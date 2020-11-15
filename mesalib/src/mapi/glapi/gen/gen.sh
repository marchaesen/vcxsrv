python3 ./glX_server_table.py -f gl_and_glX_API.xml > indirect_table.c
python3 ./glX_proto_size.py -m size_h --only-set --header-tag _INDIRECT_SIZE_H_ > indirect_size.h
python3 ./glX_proto_size.py -m size_c --only-set > indirect_size.c
python3 ./glX_proto_size.py -m size_h --only-get --header-tag _INDIRECT_SIZE_GET_H_ > indirect_size_get.h
python3 ./glX_proto_size.py -m size_c --only-get > indirect_size_get.c
python3 ./glX_proto_size.py -m reqsize_c > indirect_reqsize.c
python3 ./glX_proto_size.py -m reqsize_h --only-get --header-tag _INDIRECT_SIZE_GET_H_ > indirect_reqsize.h
python3 ./glX_proto_recv.py -m dispatch_c > indirect_dispatch.c
python3 ./glX_proto_recv.py -m dispatch_c -s > indirect_dispatch_swap.c
python3 ./glX_proto_recv.py -m dispatch_h -f gl_and_glX_API.xml -s > indirect_dispatch.h
python3 ./gl_table.py -f gl_and_es_API.xml > glapitable.h
python3 ./gl_gentable.py -f gl_and_es_API.xml > glapi_gentable.c
python3 ./gl_table.py -f gl_and_es_API.xml -m remap_table > dispatch.h
python3 ./gl_functions.py -f gl_and_es_API.xml > glfunctions.h
# ./gl_offsets.py > glapioffsets.h
python3 ./gl_apitemp.py -f gl_and_es_API.xml > glapitemp.h
python3 ./gl_procs.py -f gl_and_es_API.xml > glprocs.h

python3 ./glX_proto_send.py -m proto > indirect.c
python3 ./glX_proto_send.py -m init_h > indirect.h
python3 ./glX_proto_send.py -m init_c > indirect_init.c

python3 ./gl_enums.py -f ../registry/gl.xml > enums.c
python3 ./remap_helper.py -f gl_and_es_API.xml > remap_helper.h
cp ../../mapi_abi.py .
python3 ./mapi_abi.py --printer glapi gl_and_es_API.xml > glapi_mapi_tmp.h

