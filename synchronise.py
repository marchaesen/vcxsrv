#!/usr/bin/python
import os,sys,shutil,re,stat

from optparse import OptionParser

sys.stdout = os.fdopen(sys.stdout.fileno(), 'w', 0)

usage = "usage: %prog [options] <SrcDir> <DestDir>"
parser = OptionParser(usage=usage)
parser.add_option("-n", "--donothing", action='store_true', dest="DoNothing", default=False, help="Do not copy the files.")
parser.add_option("", "--extension", action='store', dest="Extension", default=None, help="Copy only files with this extension.")
parser.add_option("-y", "--ask", action='store_true', dest="Ask", default=False, help="Ask confirmation to copy.")
parser.add_option("-a", "--ask-file", action='append', dest="FilesToConfirm", default=[], help="Ask confirmation to copy for this speicific file (only useful if -y is not used).")
parser.add_option("-d", "--delete-file_re", dest="FilesToDeleteRe", default="", help="Regular expression of files to delete.")
parser.add_option("-s", "--no-skip-svn", dest="NoSkipSvn", action='store_true', default=False, help="Do not Skip .svn directories.")
parser.add_option("-g", "--no-skip-git", dest="NoSkipGit", action='store_true', default=False, help="Do not Skip .git directories.")
parser.add_option("-e", "--del-exist-only-dest", dest="DelExistOnlyDest", action='store_true', default=False, help="Delete files that exist only in the destination directory.")
parser.add_option( "" , "--skip-dir", dest="SkipDirs", action='append', default=[], help="Adds a directory name to skip.")
parser.add_option( "" , "--skip-file", dest="SkipFiles", action='append', default=[], help="Adds a file name to skip.")

(g_Options, g_Args) = parser.parse_args()

if g_Options.FilesToDeleteRe:
  try:
    g_FilesToDelete=re.compile(g_Options.FilesToDeleteRe,re.I)
  except:
    print 'Wrong regular expression:',g_Options.FilesToDeleteRe
    sys.exit(1)
else:
  g_FilesToDelete=None

if len(g_Args)!=2:
  parser.error("Wrong number of arguments: "+repr(g_Args))
  sys.exit(1)

g_FilesToConfirm={}
for File in g_Options.FilesToConfirm:
  g_FilesToConfirm[File]=1

g_DirsToSkip={}
for Dir in g_Options.SkipDirs:
  g_DirsToSkip[Dir]=1

g_FilesToSkip={}
for File in g_Options.SkipFiles:
  g_FilesToSkip[File]=1

g_Ask=g_Options.Ask
g_DoNothing=g_Options.DoNothing
g_NoSkipSvn=g_Options.NoSkipSvn
g_NoSkipGit=g_Options.NoSkipGit

g_WriteMask=stat.S_IWUSR|stat.S_IWGRP|stat.S_IWOTH

g_SrcDir=os.path.realpath(g_Args[0])
g_DestDir=os.path.realpath(g_Args[1])

if g_Options.Extension:
  g_Extre=re.compile('.*\.%s$'%g_Options.Extension,re.I)
else:
  g_Extre=None

###############################################################################
g_LenSrcDir=len(g_SrcDir)
if g_SrcDir[-1]!=os.path.sep:
  g_LenSrcDir+=1

def SkipDir(SrcDir,File):
  Ret = g_DirsToSkip.has_key(File)
  SrcDir=SrcDir[g_LenSrcDir:]

  while not Ret and SrcDir:
    SrcDir,Part=os.path.split(SrcDir)
    File=os.path.join(Part,File)
    Ret = g_DirsToSkip.has_key(File)
  return Ret

###############################################################################
def DeleteFiles(SrcFile,DestFile):
  print 'Deleting %s'%(SrcFile)
  os.remove(SrcFile)
  print 'Deleting %s'%(DestFile)
  try:
    os.remove(DestFile)
  except:
    print DestFile,'does not exist'

###############################################################################
#characters=['|','/','-','\\']
#chariter=iter(characters)
#def PrintBusy():
#  global characters
#  global chariter
#  
#  try:
#    char=chariter.next()
#  except StopIteration:
#    chariter=iter(characters)
#    char=chariter.next()
#
#  sys.stdout.write(char)
#  sys.stdout.write(chr(8))
count=0
def PrintBusy():
  global count
  count+=1
  item=str(count)
  sys.stdout.write(item)
  sys.stdout.write(chr(8)*len(item))
###############################################################################

if not os.path.isdir(g_DestDir):
  print g_DestDir,"is not a directory"
  sys.exit(1)

def FileDiff(SrcFile,DestFile):
  try:
    Src=open(SrcFile,'rb')
    Dest=open(DestFile,'rb')
    while 1:
      SrcLine=Src.read(4096)
      DestLine=Dest.read(4096)
      if SrcLine!=DestLine:
        return 1
      if not SrcLine:
        return 0
  except Exception,Object:
    #print "Exception occured"
    #print Object
    return 1

g_Dot=0
def SynchroniseDir(SrcDir,DestDir):
  global g_Dot
  SrcFiles=os.listdir(SrcDir)
  if g_Options.DelExistOnlyDest:
    DstFiles=os.listdir(DestDir)
    for File in DstFiles:
      DestFile=os.path.join(DestDir,File)
      try:
        Index=SrcFiles.index(File)
      except ValueError:
        if os.path.isdir(DestFile):
          if not g_NoSkipSvn and File=='.svn':
            continue   # Skip svn administration dirs
          if not g_NoSkipGit and File=='.git':
            continue   # Skip svn administration dirs
          if SkipDir(SrcDir,File):
            continue
          print 'Deleting directory',DestFile
          shutil.rmtree(DestFile)
        else:
          if g_FilesToSkip.has_key(File):
            continue
          print 'Deleting file',DestFile
          os.remove(DestFile)

  for File in SrcFiles:
    SrcFile=os.path.join(SrcDir,File)
    DestFile=os.path.join(DestDir,File)
    if os.path.isdir(SrcFile):
      if not g_NoSkipSvn and File=='.svn':
        continue   # Skip svn administration dirs
      if not g_NoSkipGit and File=='.git':
        continue   # Skip svn administration dirs
      if SkipDir(SrcDir,File):
        continue
      if not os.path.isdir(DestFile):
        if g_Dot:
          sys.stdout.write('\n')
          g_Dot=0
        if not g_DoNothing:
          if g_Ask:
            sys.stdout.write("Create directory %s? (y/n)"%DestFile)
            if sys.stdin.read(1)=='y':
              print "Creating dir",DestFile
              os.mkdir(DestFile)
            sys.stdin.read(1)
          else:
            print "Creating dir",DestFile
            os.mkdir(DestFile)
        else:
          print "Creating dir",DestFile
      SynchroniseDir(SrcFile,DestFile)
    else:
      if g_Extre and not g_Extre.search(File):
        continue
      if g_FilesToSkip.has_key(File):
        continue
      if g_FilesToDelete:
        if g_FilesToDelete.search(File):
          if g_Dot:
            sys.stdout.write('\n')
            g_Dot=0
          DeleteFiles(SrcFile,DestFile)
          continue
      if FileDiff(SrcFile,DestFile):
      #if os.system('fc /b "%s" "%s" >& nul'%(SrcFile,DestFile)):
        if g_Dot:
          sys.stdout.write('\n')
          g_Dot=0
        if not g_DoNothing:
          if g_Ask or g_FilesToConfirm.has_key(File):
            sys.stdout.write("Copy/Merge %s to %s? (y/n/m/d)"%(SrcFile,DestFile))
            R=sys.stdin.read(1)
            if R=='y':
              print '%s -> %s'%(SrcFile,DestFile)
              shutil.copyfile(SrcFile,DestFile)
            elif R=='m':
              print '%s -> %s'%(SrcFile,DestFile)
              os.system('fcg "%s" "%s"'%(SrcFile,DestFile))
            elif R=='d':
              DeleteFiles(SrcFile,DestFile)
            sys.stdin.read(1)
          else:
            print '%s -> %s'%(SrcFile,DestFile)
            # check if the file is read-only
            try:
              Mode=os.stat(DestFile).st_mode
            except OSError:
              Mode=g_WriteMask
            if not g_WriteMask&Mode:
              ReadOnly=True
              os.chmod(DestFile,Mode|g_WriteMask)
            else:
              ReadOnly=False
            shutil.copyfile(SrcFile,DestFile)
            if ReadOnly:
              os.chmod(DestFile,Mode & (~g_WriteMask) )

        else:
          print '%s -> %s'%(SrcFile,DestFile)
      else:
        PrintBusy()
        g_Dot=1

SynchroniseDir(g_SrcDir,g_DestDir)
sys.stdout.write("\n")
