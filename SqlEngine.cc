/**
 * Copyright (C) 2008 by The Regents of the University of California
 * Redistribution of this file is permitted under the terms of the GNU
 * Public License (GPL).
 *
 * @author Junghoo "John" Cho <cho AT cs.ucla.edu>
 * @date 3/24/2008
 */

#include <cstdio>
#include <iostream>
#include <fstream>
#include "Bruinbase.h"
#include "SqlEngine.h"
#include "BTreeIndex.h"
#include <string.h>
#include <stdlib.h>

using namespace std;

// external functions and variables for load file and sql command parsing 
extern FILE* sqlin;
int sqlparse(void);


RC SqlEngine::run(FILE* commandline)
{
  fprintf(stdout, "Bruinbase> ");

  // set the command line input and start parsing user input
  sqlin = commandline;
  sqlparse();  // sqlparse() is defined in SqlParser.tab.c generated from
               // SqlParser.y by bison (bison is GNU equivalent of yacc)

  return 0;
}

RC SqlEngine::select(int attr, const string& table, const vector<SelCond>& cond)
{
  RecordFile rf;   // RecordFile containing the table
  RecordId   rid;  // record cursor for table scanning
  IndexCursor c;  // cursor for navigating through the tree
  BTreeIndex tree; // BTree for indexing
  
  RC     rc;
  int    key;     
  string value;
  int    count = 0;
  int    diff;

  // open the table file
  if ((rc = rf.open(table + ".tbl", 'r')) < 0) {
  fprintf(stderr, "Error: table %s does not exist\n", table.c_str());
  return rc;
  }
  
  SelCond temp; //temporary variable for storing selection condition
  bool hasCond = false; //use to check if any valid select conditions exist
    //if false, we skip the B+ tree indexing altogether
  bool hasValueCond = false; //use to check if any value select conditions exist
    //if false, we skip the rf.read to save reads and only increment count
  bool usesIndex = false; //assume B+ tree is not used (used for closing index file)
  
  //boolean, min, and max values for filtering the key, if applicable
  bool condGE = false; //true means key is >= min
  bool condLE = false; //true means key is <= max
  int max = -1;
  int min = -1;
  int eqVal = -1; //if not -1, the key must be equal to this
  
  //keep track of the index of a vital select condition
  int condIndex = -1;
  
  //check if any value conditions are conflicting
  bool valueConflict = false;
  std::string valEq = "";
  
  //go through all SelConds in parameter to determine conditions
  for(int i=0; i<cond.size(); i++)
  {
    temp = cond[i];
    int tempVal = atoi(temp.value); //store the int-form value of the select condition
     
    //we only have to worry about conditions on keys that don't involve NE
    //all other select conditions will be considered outside of the B+ Tree
    if(temp.attr==1 && temp.comp!=SelCond::NE)
    {
      hasCond = true; //if we ever hit a valid condition, set this to true
      
      if(temp.comp==SelCond::EQ) //if there's ever an equality on key, that is the only select condition; break
      {
        eqVal = tempVal;
        condIndex = i;
        break;
      }
      else if(temp.comp==SelCond::GE) //key >= min
      {
        if(tempVal > min || min==-1) //if the tempVal min is larger than our current min (or it's uninitialized), set GE
        {
          condGE = true;
          min = tempVal;
        }
      }
      else if(temp.comp==SelCond::GT) //key > min
      {
        if(tempVal >= min || min==-1) //if the tempVal min is larger than or equal to our current min (or it's uninitialized), set GT
        {
          condGE = false;
          min = tempVal;
        }
      }
      else if(temp.comp==SelCond::LE) //key <= max
      {
        if(tempVal < max || max==-1) //if the tempVal max is smaller than our current max (or it's uninitialized), set LE
        {
          condLE = true;
          max = tempVal;
        }
      }
      else if(temp.comp==SelCond::LT) //key < max
      {
        if(tempVal <= max || max==-1) //if the tempVal max is smaller than or equal to our current max (or it's uninitialized), set LT
        {
          condLE = false;
          max = tempVal;
        }
      }
    }
    else if(temp.attr==2) //if we hit a value condition, update hasValueCond and check for contradictions
    {
      hasValueCond = true;
      
      if(temp.comp==SelCond::EQ) //check on value equality
      {
        if(valEq=="" || strcmp(value.c_str(), cond[i].value)==0) //if no value equality conditions have been specified yet or this condition is the same as one before
          valEq=tempVal;
        else
          valueConflict = true;
      }
    }
  }
  
  //the next two if-statements give the query a chance to end early (if there are no tuple matches)
  //check if the conditions are already impossible
  if(valueConflict || (max!=-1 && min!=-1 && max<min))
    goto end_select_early;
    
  //similarly, if max=min but there is neither less-than-equal or greater-than-equal, no results
  if(max!=-1 && min!=-1 && !condGE && !condLE && max==min)
    goto end_select_early;
  
  //if the index file does not exist, use normal select
  //similarly, unless we are interested in a count(*) without conditions, an empty condition array means we use normal select
  //we do this because using "select count(*) from table" could offer a speedup using the index tfile
  if(tree.open(table + ".idx", 'r')!=0 || (!hasCond && attr!=4))
  {
    // scan the table file from the beginning
    rid.pid = rid.sid = 0;
    count = 0;
    while (rid < rf.endRid()) {
    // read the tuple
    if ((rc = rf.read(rid, key, value)) < 0) {
      fprintf(stderr, "Error: while reading a tuple from table %s\n", table.c_str());
      goto exit_select;
    }

    // check the conditions on the tuple
    for (unsigned i = 0; i < cond.size(); i++) {
      // compute the difference between the tuple value and the condition value
      switch (cond[i].attr) {
      case 1:
    diff = key - atoi(cond[i].value);
    break;
      case 2:
    diff = strcmp(value.c_str(), cond[i].value);
    break;
      }

      // skip the tuple if any condition is not met
      switch (cond[i].comp) {
        case SelCond::EQ:
        if (diff != 0) goto next_tuple;
        break;
        case SelCond::NE:
        if (diff == 0) goto next_tuple;
        break;
        case SelCond::GT:
        if (diff <= 0) goto next_tuple;
        break;
        case SelCond::LT:
        if (diff >= 0) goto next_tuple;
        break;
        case SelCond::GE:
        if (diff < 0) goto next_tuple;
        break;
        case SelCond::LE:
        if (diff > 0) goto next_tuple;
        break;
      }
    }

    // the condition is met for the tuple. 
    // increase matching tuple counter
    count++;

    // print the tuple 
    switch (attr) {
      case 1:  // SELECT key
        fprintf(stdout, "%d\n", key);
        break;
      case 2:  // SELECT value
        fprintf(stdout, "%s\n", value.c_str());
        break;
      case 3:  // SELECT *
        fprintf(stdout, "%d '%s'\n", key, value.c_str());
        break;
    }

    // move to the next tuple
    next_tuple:
    ++rid;
    }
  }
  else //otherwise, table's index file exists!
  {
  //initialize variables (rid doesn't really matter here)
  count = 0;
  rid.pid = rid.sid = 0;
  usesIndex = true; //set this in order to close index properly
  
  //set the starting position for IndexCursor c
  if(eqVal!=-1) //key must be eqVal
    tree.locate(eqVal, c);
  else if(min!=-1 && !condGE) //key must be greater than min
    tree.locate(min+1, c);
  else if(min!=-1 && condGE) //key must be at least min
    tree.locate(min, c);
  else
    tree.locate(0, c);
  
  while(tree.readForward(c, key, rid)==0)
  {
    if(!hasValueCond && attr==4) //no need to read from disk for value
    {
      if(eqVal!=-1 && key!=eqVal) //if there is a condition on key equality that doesn't match, we are done
        goto end_select_early;
      
      if(max!=-1) //if there is a condition on LT or LE that fails, we are done
      {
        if(condLE && key>max)
          goto end_select_early;
        else if(!condLE && key>=max)
          goto end_select_early;
      }
      
      if(min!=-1) //if there is a condition on GT or GE that fails, we are done
      {
        if(condGE && key<min)
          goto end_select_early;
        else if(!condGE && key<=min)
          goto end_select_early;
      }
      
      //if key passes all of the conditions, increment count and jump to next cycle in while loop
      //in doing this if-statement, we save many reads from record file
      count++;
      continue;
    }
  
    // read the tuple (same as given code)
    if ((rc = rf.read(rid, key, value)) < 0) {
      fprintf(stderr, "Error: while reading a tuple from table %s\n", table.c_str());
      goto exit_select;
    }

    // check the conditions on the tuple (modified from given code)
    for (unsigned i = 0; i < cond.size(); i++)
    {
      // compute the difference between the tuple value and the condition value
      switch (cond[i].attr)
      {
        //if condition on key
        case 1:
          diff = key - atoi(cond[i].value);
          break;
        //if condition on value
        case 2:
          diff = strcmp(value.c_str(), cond[i].value);
          break;
      }

      //skip the tuple if any condition is not met
      //for less than (LT) or less than or equal (LE) conditions on keys, once one of them fails, we are done
      //since the tree index is sorted, every key afterwards is also going to fail the condition
      //the same applies with EQ, since once we're past the key that was equal, everything else will fail also
      switch (cond[i].comp)
      {
        case SelCond::EQ:
          if (diff != 0)
          {
            if(cond[i].attr==1)
              goto end_select_early;
            goto continue_while;
          }
          break;
        case SelCond::NE:
          if (diff == 0) goto continue_while; //if keys ever match when they're not supposed to, break out of for-loop and wait for next cursor
          break;
        case SelCond::GT:
          if (diff <= 0) goto continue_while; //if !(key > cond value), break out of for-loop and wait for next cursor
          break;
        case SelCond::LT:
          if (diff >= 0)
          {
            if(cond[i].attr==1) //if this ever fails on a key, everything else after will fail anyway, so we end
              goto end_select_early;
            goto continue_while;
          }
          break;
        case SelCond::GE:
          if (diff < 0) goto continue_while; //if !(key >= cond value), break out of for-loop and wait for next cursor
          break;
        case SelCond::LE:
          if (diff > 0)
          {
            if(cond[i].attr==1) //if this ever fails on a key, everything else after will fail anyway, so we end
              goto end_select_early;
            goto continue_while;
          }
          break;
      }
    }

    // the condition is met for the tuple. 
    // increase matching tuple counter
    count++;

    // print the tuple 
    switch (attr)
    {
      case 1:  // SELECT key
        fprintf(stdout, "%d\n", key);
        break;
      case 2:  // SELECT value
        fprintf(stdout, "%s\n", value.c_str());
        break;
      case 3:  // SELECT *
        fprintf(stdout, "%d '%s'\n", key, value.c_str());
        break;
    }
    
    continue_while:
    cout << ""; //do nothing; we use this to jump to the next while cycle
  }
  }
  
  end_select_early: //use to leave the while loop in case some tuple fails select conditions
  
  // print matching tuple count if "select count(*)"
  if (attr == 4) {
    fprintf(stdout, "%d\n", count);
  }
  rc = 0;

  // close the table file and return
  exit_select:
  
  if(usesIndex)
  tree.close(); //close the index file if applicable
  
  rf.close();
  return rc;
}

RC SqlEngine::load(const string& table, const string& loadfile, bool index)
{
  RecordFile rf;   // RecordFile containing the table
  RecordId   rid;  // record cursor for table scanning
  RC     rc;
  BTreeIndex tree;  //BTreeIndex for indexing (if applicable)
  
  string line; // string holding each line from loadfile
  int    key; // holds key as parsed from line's tuple pair
  string value; //holds value as parsed from line's tuple pair
  
  //open loadfile as fstream
  ifstream tableData(loadfile.c_str());
  
  //check that provided loadfile can be opened
  if(!tableData.is_open())
  fprintf(stderr, "Error: loadfile %s cannot be opened\n", loadfile.c_str());
  
  //open or create specified table file
  rc = rf.open(table + ".tbl", 'w');
  
  //check index for making BTree
  if(index)
  {
  //open and write to BTreeIndex as tablename.idx
  tree.open(table + ".idx", 'w');
  
  //get every line from loadfile
    while(getline(tableData, line))
    {
    parseLoadLine(line, key, value);
    if(rf.append(key, value, rid)!=0)
      return RC_FILE_WRITE_FAILED;
    
    //insert (key, rid) pair into BTree for indexing
    //check for errors in the meantime
    if(tree.insert(key, rid)!=0)
      return RC_FILE_WRITE_FAILED;
    }
    
    //tree.print();
  
  //close the index tree and file
  tree.close();
  }
  else
  {
    //get every line from loadfile
    while(getline(tableData, line))
    {
    parseLoadLine(line, key, value);
    rc = rf.append(key, value, rid);  
    }
  }
  
  //close RecordFile and the loadfile
  rf.close();
  tableData.close();
  
  return rc;
}

RC SqlEngine::parseLoadLine(const string& line, int& key, string& value)
{
    const char *s;
    char        c;
    string::size_type loc;
    
    // ignore beginning white spaces
    c = *(s = line.c_str());
    while (c == ' ' || c == '\t') { c = *++s; }

    // get the integer key value
    key = atoi(s);

    // look for comma
    s = strchr(s, ',');
    if (s == NULL) { return RC_INVALID_FILE_FORMAT; }

    // ignore white spaces
    do { c = *++s; } while (c == ' ' || c == '\t');
    
    // if there is nothing left, set the value to empty string
    if (c == 0) { 
        value.erase();
        return 0;
    }

    // is the value field delimited by ' or "?
    if (c == '\'' || c == '"') {
        s++;
    } else {
        c = '\n';
    }

    // get the value string
    value.assign(s);
    loc = value.find(c, 0);
    if (loc != string::npos) { value.erase(loc); }

    return 0;
}