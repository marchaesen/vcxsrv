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

#ifndef __RULE_H__
#define __RULE_H__

#include "refptr.h"
#include "md5.h"
class mhmakefileparser;
class fileinfo;

class rule: public refbase
{
  vector<string> m_Commands;
  string m_Stem;   // Contains the stem in case the rule is part of an implicit rule (filled in in the implicit search)
  mhmakefileparser* m_pMakefile;
  vector< fileinfo* > m_Targets;  /* Targets that are build with this rule, do not use refptr here because otherwise we get circular references */
public:
  rule(mhmakefileparser *pMakefile): m_pMakefile(pMakefile)
  {
  }

  void AddCommand(const string &Command)
  {
    if (!Command.empty())
      m_Commands.push_back(Command);
  }
  vector<string>& GetCommands()
  {
    return m_Commands;
  }
  void PrintCommands(fileinfo *pTarget=NULL) const;

  void SetStem(const string &Stem)
  {
    m_Stem=Stem;
  }
  const string &GetStem() const
  {
    return m_Stem;
  }
  void SetMakefile(mhmakefileparser *pMakefile)
  {
    m_pMakefile=pMakefile;
  }
  mhmakefileparser *GetMakefile()
  {
    return m_pMakefile;
  }
  bool operator != (const rule &Rule);

  void AddTarget(fileinfo *pTarget)
  {
    m_Targets.push_back(pTarget);
  }
  void SetTargetsIsBuild(uint32 Md5_32);
  void SetTargetsIsBuilding(const fileinfo *pSrc);
};

typedef vector<pair<vector<fileinfo*>,refptr<rule> > > implicitruledep_t;
typedef pair<fileinfo *, implicitruledep_t > implicitrule_t;

class IMPLICITRULE
{
  static set<rule*> m_ImplicitRuleRecurseDetStack;
  static vector<implicitrule_t> m_ImplicitRules;  // Use a vector and not a map because the order of the implicit rules is important
public:
  static void AddImplicitRule(fileinfo *pTarget,const vector<fileinfo*> &Deps,refptr<rule> pRule);
  static void SearchImplicitRule(const fileinfo *pTarget, implicitruledep_t &Result);
  static void PrintImplicitRules();
  static bool PushRule(rule *pRule)
  {
    set<rule*>::iterator pFound=m_ImplicitRuleRecurseDetStack.find(pRule);
    if (pFound==m_ImplicitRuleRecurseDetStack.end())
    {
      m_ImplicitRuleRecurseDetStack.insert(pRule);
      return false;
    }
    else
    {
      return true;
    }
  }
  static void PopRule(rule *pRule)
  {
    m_ImplicitRuleRecurseDetStack.erase(pRule);
  }
};

extern refptr<rule> NullRule;

#endif

