import sys,re

infile=open(sys.argv[1],"r")
inbuffer=infile.read()
infile.close()
outfile=open(sys.argv[1],"w")
inbuffer=re.sub(r'\(int\)\(long\)&\(',r'(int)(intptr_t)&(',inbuffer)
outfile.write(inbuffer)
outfile.close()

