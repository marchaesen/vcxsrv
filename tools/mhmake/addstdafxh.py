import sys,re,os

try:
  InputFile=sys.argv[1]
except:
  print("No input file specified")
  sys.exit(1)

try:
  InputFile=sys.argv[1]
  pIN=open(InputFile,"r")
  data=pIN.read()
  pIN.close()
except:
  print("Error reading input file",InputFile)
  sys.exit(1)

try:
  pOUT=open(InputFile,"w")
except:
  print("Error opening",InputFile,"for writing")
  sys.exit(1)

pOUT.write("""#include "stdafx.h"\n""")
#since we are going to add one line we need to update all line statements
def IncrementLine(matchobj):
  return "#line %d%s"%(int(matchobj.group(1))+1,matchobj.group(2))

data=re.sub(r"#\s*line\s+(\d+)(.*%s)"%os.path.split(InputFile)[1],IncrementLine,data)

pOUT.write(data)
pOUT.close()

