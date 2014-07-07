/*  This file is part of mhmake.
 *
 *  Copyright (C) 2001-2010 marha@sourceforge.net
 *
 *  Mhmake is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Mhmake is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Mhmake.  If not, see <http://www.gnu.org/licenses/>.
*/

/* $Rev$ */

#include "stdafx.h"

#include "fileinfo.h"
#include "curdir.h"
#include "util.h"
#include "mhmakeparser.hpp"

fileinfos g_FileInfos;  // declare here since it is important that it is constructed before m_pcurrentdir
curdir::initcurdir curdir::m_pCurrentDir;
makecommand g_MakeCommand;  // Order is important since sm_Statics is using g_MakeCommand
const string g_QuoteString("\""); // Order is important since sm_Statics is using g_QuoteString
loadedmakefile::loadedmakefile_statics loadedmakefile::sm_Statics;  // Declare this here because this constructor is using g_FileInfos and m_pCurrentDir
LOADEDMAKEFILES g_LoadedMakefiles;  // Declare it here otherwize problems in the destructor (still using items from g_FileInfos)

///////////////////////////////////////////////////////////////////////////////
curdir::initcurdir::initcurdir()
{
  char CurDir[MAX_PATH];
  if (!getcwd(CurDir,MAX_PATH))
    throw string("Error getting current directory.");
  string strCurDir=CurDir;
  m_pDir=GetAbsFileInfo(NormalizePathName(strCurDir));
}
