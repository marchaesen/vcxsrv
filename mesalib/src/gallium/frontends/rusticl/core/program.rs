use crate::api::icd::*;
use crate::core::context::*;
use crate::core::device::*;
use crate::impl_cl_type_trait;

use mesa_rust::compiler::clc::*;
use mesa_rust::compiler::nir::*;
use mesa_rust::util::disk_cache::*;
use mesa_rust_gen::*;
use rusticl_opencl_gen::*;

use std::collections::HashMap;
use std::collections::HashSet;
use std::ffi::CString;
use std::mem::size_of;
use std::ptr;
use std::slice;
use std::sync::atomic::AtomicU32;
use std::sync::atomic::Ordering;
use std::sync::Arc;
use std::sync::Mutex;
use std::sync::MutexGuard;
use std::sync::Once;

const BIN_HEADER_SIZE_V1: usize =
    // 1. format version
    size_of::<u32>() +
    // 2. spirv len
    size_of::<u32>() +
    // 3. binary_type
    size_of::<cl_program_binary_type>();

const BIN_HEADER_SIZE: usize = BIN_HEADER_SIZE_V1;

// kernel cache
static mut DISK_CACHE: Option<DiskCache> = None;
static DISK_CACHE_ONCE: Once = Once::new();

fn get_disk_cache() -> &'static Option<DiskCache> {
    unsafe {
        DISK_CACHE_ONCE.call_once(|| {
            DISK_CACHE = DiskCache::new("rusticl", "rusticl", 0);
        });
        &DISK_CACHE
    }
}

#[repr(C)]
pub struct Program {
    pub base: CLObjectBase<CL_INVALID_PROGRAM>,
    pub context: Arc<Context>,
    pub devs: Vec<Arc<Device>>,
    pub src: CString,
    pub il: Vec<u8>,
    pub kernel_count: AtomicU32,
    spec_constants: Mutex<Vec<spirv::SpecConstant>>,
    build: Mutex<ProgramBuild>,
}

impl_cl_type_trait!(cl_program, Program, CL_INVALID_PROGRAM);

struct ProgramBuild {
    builds: HashMap<Arc<Device>, ProgramDevBuild>,
    kernels: Vec<String>,
}

struct ProgramDevBuild {
    spirv: Option<spirv::SPIRVBin>,
    status: cl_build_status,
    options: String,
    log: String,
    bin_type: cl_program_binary_type,
}

fn prepare_options(options: &str, dev: &Device) -> Vec<CString> {
    let mut options = options.to_owned();
    if !options.contains("-cl-std=CL") {
        options.push_str(" -cl-std=CL");
        options.push_str(dev.clc_version.api_str());
    }
    if !dev.image_supported() {
        options.push_str(" -U__IMAGE_SUPPORT__");
    }
    options.push_str(" -D__OPENCL_VERSION__=");
    options.push_str(dev.cl_version.clc_str());

    let mut res = Vec::new();

    // we seperate on a ' ' unless we hit a "
    let mut sep = ' ';
    let mut old = 0;
    for (i, c) in options.char_indices() {
        if c == '"' {
            if sep == ' ' {
                sep = '"';
            } else {
                sep = ' ';
            }
        }

        if c == '"' || c == sep {
            // beware of double seps
            if old != i {
                res.push(&options[old..i]);
            }
            old = i + c.len_utf8();
        }
    }
    // add end of the string
    res.push(&options[old..]);

    res.iter()
        .map(|&a| match a {
            "-cl-denorms-are-zero" => "-fdenormal-fp-math=positive-zero",
            _ => a,
        })
        .map(CString::new)
        .map(Result::unwrap)
        .collect()
}

impl Program {
    pub fn new(context: &Arc<Context>, devs: &[Arc<Device>], src: CString) -> Arc<Program> {
        let builds = devs
            .iter()
            .map(|d| {
                (
                    d.clone(),
                    ProgramDevBuild {
                        spirv: None,
                        status: CL_BUILD_NONE,
                        log: String::from(""),
                        options: String::from(""),
                        bin_type: CL_PROGRAM_BINARY_TYPE_NONE,
                    },
                )
            })
            .collect();

        Arc::new(Self {
            base: CLObjectBase::new(),
            context: context.clone(),
            devs: devs.to_vec(),
            src: src,
            il: Vec::new(),
            kernel_count: AtomicU32::new(0),
            spec_constants: Mutex::new(Vec::new()),
            build: Mutex::new(ProgramBuild {
                builds: builds,
                kernels: Vec::new(),
            }),
        })
    }

    pub fn from_bins(
        context: Arc<Context>,
        devs: Vec<Arc<Device>>,
        bins: &[&[u8]],
    ) -> Arc<Program> {
        let mut builds = HashMap::new();
        let mut kernels = HashSet::new();

        for (d, b) in devs.iter().zip(bins) {
            let mut ptr = b.as_ptr();
            let bin_type;
            let spirv;

            unsafe {
                // 1. version
                let version = ptr.cast::<u32>().read();
                ptr = ptr.add(size_of::<u32>());

                match version {
                    1 => {
                        // 2. size of the spirv
                        let spirv_size = ptr.cast::<u32>().read();
                        ptr = ptr.add(size_of::<u32>());

                        // 3. binary_type
                        bin_type = ptr.cast::<cl_program_binary_type>().read();
                        ptr = ptr.add(size_of::<cl_program_binary_type>());

                        // 4. the spirv
                        assert!(b.as_ptr().add(BIN_HEADER_SIZE_V1) == ptr);
                        assert!(b.len() == BIN_HEADER_SIZE_V1 + spirv_size as usize);
                        spirv = Some(spirv::SPIRVBin::from_bin(slice::from_raw_parts(
                            ptr,
                            spirv_size as usize,
                        )));
                    }
                    _ => panic!("unknown version"),
                }
            }

            if let Some(spirv) = &spirv {
                for k in spirv.kernels() {
                    kernels.insert(k);
                }
            }

            builds.insert(
                d.clone(),
                ProgramDevBuild {
                    spirv: spirv,
                    status: CL_BUILD_SUCCESS as cl_build_status,
                    log: String::from(""),
                    options: String::from(""),
                    bin_type: bin_type,
                },
            );
        }

        Arc::new(Self {
            base: CLObjectBase::new(),
            context: context,
            devs: devs,
            src: CString::new("").unwrap(),
            il: Vec::new(),
            kernel_count: AtomicU32::new(0),
            spec_constants: Mutex::new(Vec::new()),
            build: Mutex::new(ProgramBuild {
                builds: builds,
                kernels: kernels.into_iter().collect(),
            }),
        })
    }

    pub fn from_spirv(context: Arc<Context>, spirv: &[u8]) -> Arc<Program> {
        let mut builds = HashMap::new();

        for d in &context.devs {
            let spirv = Some(spirv::SPIRVBin::from_bin(spirv));

            builds.insert(
                d.clone(),
                ProgramDevBuild {
                    spirv: spirv,
                    status: CL_BUILD_SUCCESS as cl_build_status,
                    log: String::from(""),
                    options: String::from(""),
                    bin_type: CL_PROGRAM_BINARY_TYPE_INTERMEDIATE,
                },
            );
        }

        Arc::new(Self {
            base: CLObjectBase::new(),
            devs: context.devs.clone(),
            context: context,
            src: CString::new("").unwrap(),
            il: spirv.to_vec(),
            kernel_count: AtomicU32::new(0),
            spec_constants: Mutex::new(Vec::new()),
            build: Mutex::new(ProgramBuild {
                builds: builds,
                kernels: Vec::new(),
            }),
        })
    }

    fn build_info(&self) -> MutexGuard<ProgramBuild> {
        self.build.lock().unwrap()
    }

    fn dev_build_info<'a>(
        l: &'a mut MutexGuard<ProgramBuild>,
        dev: &Arc<Device>,
    ) -> &'a mut ProgramDevBuild {
        l.builds.get_mut(dev).unwrap()
    }

    pub fn status(&self, dev: &Arc<Device>) -> cl_build_status {
        Self::dev_build_info(&mut self.build_info(), dev).status
    }

    pub fn log(&self, dev: &Arc<Device>) -> String {
        Self::dev_build_info(&mut self.build_info(), dev)
            .log
            .clone()
    }

    pub fn bin_type(&self, dev: &Arc<Device>) -> cl_program_binary_type {
        Self::dev_build_info(&mut self.build_info(), dev).bin_type
    }

    pub fn options(&self, dev: &Arc<Device>) -> String {
        Self::dev_build_info(&mut self.build_info(), dev)
            .options
            .clone()
    }

    // we need to precalculate the size
    pub fn bin_sizes(&self) -> Vec<usize> {
        let mut lock = self.build_info();
        let mut res = Vec::new();
        for d in &self.devs {
            let info = Self::dev_build_info(&mut lock, d);

            res.push(
                info.spirv
                    .as_ref()
                    .map_or(0, |s| s.to_bin().len() + BIN_HEADER_SIZE),
            );
        }
        res
    }

    pub fn binaries(&self, vals: &[u8]) -> Vec<*mut u8> {
        // if the application didn't provide any pointers, just return the length of devices
        if vals.is_empty() {
            return vec![std::ptr::null_mut(); self.devs.len()];
        }

        // vals is an array of pointers where we should write the device binaries into
        if vals.len() != self.devs.len() * size_of::<*const u8>() {
            panic!("wrong size")
        }

        let ptrs: &[*mut u8] = unsafe {
            slice::from_raw_parts(vals.as_ptr().cast(), vals.len() / size_of::<*mut u8>())
        };

        let mut lock = self.build_info();
        for (i, d) in self.devs.iter().enumerate() {
            let mut ptr = ptrs[i];
            let info = Self::dev_build_info(&mut lock, d);
            let spirv = info.spirv.as_ref().unwrap().to_bin();

            unsafe {
                // 1. binary format version
                ptr.cast::<u32>().write(1);
                ptr = ptr.add(size_of::<u32>());

                // 2. size of the spirv
                ptr.cast::<u32>().write(spirv.len() as u32);
                ptr = ptr.add(size_of::<u32>());

                // 3. binary_type
                ptr.cast::<cl_program_binary_type>().write(info.bin_type);
                ptr = ptr.add(size_of::<cl_program_binary_type>());

                // 4. the spirv
                assert!(ptrs[i].add(BIN_HEADER_SIZE) == ptr);
                ptr::copy_nonoverlapping(spirv.as_ptr(), ptr, spirv.len());
            }
        }

        ptrs.to_vec()
    }

    pub fn args(&self, dev: &Arc<Device>, kernel: &str) -> Vec<spirv::SPIRVKernelArg> {
        Self::dev_build_info(&mut self.build_info(), dev)
            .spirv
            .as_ref()
            .unwrap()
            .args(kernel)
    }

    pub fn kernels(&self) -> Vec<String> {
        self.build_info().kernels.clone()
    }

    pub fn active_kernels(&self) -> bool {
        self.kernel_count.load(Ordering::Relaxed) != 0
    }

    pub fn build(&self, dev: &Arc<Device>, options: String) -> bool {
        // program binary
        let is_il = !self.il.is_empty();
        if self.src.as_bytes().is_empty() && !is_il {
            return true;
        }

        let mut info = self.build_info();
        let d = Self::dev_build_info(&mut info, dev);
        let lib = options.contains("-create-library");
        let args = prepare_options(&options, dev);

        if !is_il {
            let (spirv, log) = spirv::SPIRVBin::from_clc(
                &self.src,
                &args,
                &Vec::new(),
                get_disk_cache(),
                dev.cl_features(),
            );

            d.log = log;
            if spirv.is_none() {
                d.status = CL_BUILD_ERROR;
                return false;
            }
            d.spirv = spirv;
        }

        d.options = options;

        let spirvs = [d.spirv.as_ref().unwrap()];
        let (spirv, log) = spirv::SPIRVBin::link(&spirvs, lib);

        d.log.push_str(&log);
        d.spirv = spirv;
        if d.spirv.is_some() {
            d.bin_type = if lib {
                CL_PROGRAM_BINARY_TYPE_LIBRARY
            } else {
                CL_PROGRAM_BINARY_TYPE_EXECUTABLE
            };
            d.status = CL_BUILD_SUCCESS as cl_build_status;
            let mut kernels = d.spirv.as_ref().unwrap().kernels();
            info.kernels.append(&mut kernels);
            true
        } else {
            d.status = CL_BUILD_ERROR;
            false
        }
    }

    pub fn compile(
        &self,
        dev: &Arc<Device>,
        options: String,
        headers: &[spirv::CLCHeader],
    ) -> bool {
        // program binary
        let is_il = !self.il.is_empty();
        if self.src.as_bytes().is_empty() && !is_il {
            return true;
        }

        let mut info = self.build_info();
        let d = Self::dev_build_info(&mut info, dev);

        if is_il {
            d.bin_type = CL_PROGRAM_BINARY_TYPE_COMPILED_OBJECT;
            return true;
        }

        let args = prepare_options(&options, dev);

        let (spirv, log) = spirv::SPIRVBin::from_clc(
            &self.src,
            &args,
            headers,
            get_disk_cache(),
            dev.cl_features(),
        );

        d.spirv = spirv;
        d.log = log;
        d.options = options;

        if d.spirv.is_some() {
            d.status = CL_BUILD_SUCCESS as cl_build_status;
            d.bin_type = CL_PROGRAM_BINARY_TYPE_COMPILED_OBJECT;
            true
        } else {
            d.status = CL_BUILD_ERROR;
            false
        }
    }

    pub fn link(
        context: Arc<Context>,
        devs: &[Arc<Device>],
        progs: &[Arc<Program>],
        options: String,
    ) -> Arc<Program> {
        let devs: Vec<Arc<Device>> = devs.iter().map(|d| (*d).clone()).collect();
        let mut builds = HashMap::new();
        let mut kernels = HashSet::new();
        let mut locks: Vec<_> = progs.iter().map(|p| p.build_info()).collect();
        let lib = options.contains("-create-library");

        for d in &devs {
            let bins: Vec<_> = locks
                .iter_mut()
                .map(|l| Self::dev_build_info(l, d).spirv.as_ref().unwrap())
                .collect();

            let (spirv, log) = spirv::SPIRVBin::link(&bins, lib);

            let status;
            let bin_type;
            if let Some(spirv) = &spirv {
                for k in spirv.kernels() {
                    kernels.insert(k);
                }
                status = CL_BUILD_SUCCESS as cl_build_status;
                bin_type = if lib {
                    CL_PROGRAM_BINARY_TYPE_LIBRARY
                } else {
                    CL_PROGRAM_BINARY_TYPE_EXECUTABLE
                };
            } else {
                status = CL_BUILD_ERROR;
                bin_type = CL_PROGRAM_BINARY_TYPE_NONE;
            };

            builds.insert(
                d.clone(),
                ProgramDevBuild {
                    spirv: spirv,
                    status: status,
                    log: log,
                    options: String::from(""),
                    bin_type: bin_type,
                },
            );
        }

        Arc::new(Self {
            base: CLObjectBase::new(),
            context: context,
            devs: devs,
            src: CString::new("").unwrap(),
            il: Vec::new(),
            kernel_count: AtomicU32::new(0),
            spec_constants: Mutex::new(Vec::new()),
            build: Mutex::new(ProgramBuild {
                builds: builds,
                kernels: kernels.into_iter().collect(),
            }),
        })
    }

    pub(super) fn hash_key(&self, dev: &Arc<Device>, name: &str) -> Option<cache_key> {
        if let Some(cache) = dev.screen().shader_cache() {
            let mut lock = self.build_info();
            let info = Self::dev_build_info(&mut lock, dev);
            assert_eq!(info.status, CL_BUILD_SUCCESS as cl_build_status);

            let spirv = info.spirv.as_ref().unwrap();
            let mut bin = spirv.to_bin().to_vec();
            bin.extend_from_slice(name.as_bytes());
            Some(cache.gen_key(&bin))
        } else {
            None
        }
    }

    pub fn devs_with_build(&self) -> Vec<&Arc<Device>> {
        let mut lock = self.build_info();
        self.devs
            .iter()
            .filter(|d| {
                let info = Self::dev_build_info(&mut lock, d);
                info.status == CL_BUILD_SUCCESS as cl_build_status
            })
            .collect()
    }

    pub fn attribute_str(&self, kernel: &str, d: &Arc<Device>) -> String {
        let mut lock = self.build_info();
        let info = Self::dev_build_info(&mut lock, d);

        let attributes_strings = [
            info.spirv.as_ref().unwrap().vec_type_hint(kernel),
            info.spirv.as_ref().unwrap().local_size(kernel),
            info.spirv.as_ref().unwrap().local_size_hint(kernel),
        ];

        let attributes_strings: Vec<_> = attributes_strings
            .iter()
            .flatten()
            .map(String::as_str)
            .collect();
        attributes_strings.join(",")
    }

    pub fn to_nir(&self, kernel: &str, d: &Arc<Device>) -> NirShader {
        let mut lock = self.build_info();
        let info = Self::dev_build_info(&mut lock, d);
        assert_eq!(info.status, CL_BUILD_SUCCESS as cl_build_status);
        info.spirv
            .as_ref()
            .unwrap()
            .to_nir(
                kernel,
                d.screen
                    .nir_shader_compiler_options(pipe_shader_type::PIPE_SHADER_COMPUTE),
                &d.lib_clc,
                &mut [],
            )
            .unwrap()
    }

    pub fn is_binary(&self) -> bool {
        self.src.to_bytes().is_empty() && self.il.is_empty()
    }
}
