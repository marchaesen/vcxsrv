set -x
python3 ./glX_proto_size.py -m size_h --only-set --header-tag _INDIRECT_SIZE_H_ > indirect_size.h
python3 ./glX_proto_size.py -m size_c --only-set > indirect_size.c
python3 ./glX_proto_size.py -m size_h --only-get --header-tag _INDIRECT_SIZE_GET_H_ > indirect_size_get.h
python3 ./glX_proto_size.py -m size_c --only-get > indirect_size_get.c
python3 ./gl_table.py -f gl_and_es_API.xml > glapitable.h
python3 ./gl_gentable.py -f gl_and_es_API.xml > glapi_gentable.c
python3 ./gl_table.py -f gl_and_es_API.xml -m dispatch > dispatch.h
# ./gl_offsets.py > glapioffsets.h
python3 ./gl_apitemp.py -f gl_and_es_API.xml > glapitemp.h
python3 ./gl_procs.py -f gl_and_es_API.xml > glprocs.h

python3 ./glX_proto_send.py -m proto > indirect.c
python3 ./glX_proto_send.py -m init_h > indirect.h
python3 ./glX_proto_send.py -m init_c > indirect_init.c

python3 ./gl_enums.py -f ../registry/gl.xml > enums.c
cp ../../mapi_abi.py .
python3 ./mapi_abi.py --printer glapi gl_and_es_API.xml > glapi_mapi_tmp.h

