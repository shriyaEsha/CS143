/*
 * Copyright (C) 2008 by The Regents of the University of California
 * Redistribution of this file is permitted under the terms of the GNU
 * Public License (GPL).
 *
 * @author Junghoo "John" Cho <cho AT cs.ucla.edu>
 * @date 3/24/2008
 */
 
#include "BTreeIndex.h"
#include "BTreeNode.h"

using namespace std;

/*
 * BTreeIndex constructor
 */
BTreeIndex::BTreeIndex()
{
    rootPid = -1;
    treeHeight = 0;
    // clear buffer
    fill(buffer,buffer+PageFile::PAGE_SIZE,'\0');
}

/*
 * Open the index file in read or write mode.
 * Under 'w' mode, the index file should be created if it does not exist.
 * @param indexname[IN] the name of the index file
 * @param mode[IN] 'r' for read, 'w' for write
 * @return error code. 0 if no error
 */
RC BTreeIndex::open(const string& indexname, char mode)
{
   	if(pf.open(indexname,mode)!=0)
   		return -1;
   	// init variables if opening for first time
   	if(pf.endPid()==0)
   	{
   		treeHeight = 0;
   		rootPid = -1;
   		if(pf.write(0,buffer)!=0)
   			return -1;
   		return 0;
   	}
   	// read index file
   	if(pf.read(0,buffer)!=0)
   		return -1;
   	// get rootPid and height from index file
   	int pid = -1;
   	int ht = -1;
   	memcpy(&pid,buffer,sizeof(int));
   	memcpy(&ht,buffer+sizeof(int),sizeof(int));
   	if(pid > 0 && ht >= 0)
   	{
   		rootPid = pid;
   		treeHeight = ht;
   	}
   	return 0;
}

/*
 * Close the index file.
 * @return error code. 0 if no error
 */
RC BTreeIndex::close()
{
    // save variable to index file before closing
    memcpy(buffer,&rootPid,sizeof(int));
    memcpy(buffer+sizeof(int),&treeHeight,sizeof(int));
    if(pf.write(0,buffer) != 0)
    	return -1;
    return pf.close();
}

/*
 * Insert (key, RecordId) pair to the index.
 * @param key[IN] the key for the value inserted into the index
 * @param rid[IN] the RecordId for the record being inserted into the index
 * @return error code. 0 if no error
 */
RC BTreeIndex::insert_recursive(int key, const RecordId& rid, PageId curPid, PageId &sibling_pid, int &sibling_key, int level)
{
	RC error;
	sibling_key = -1;
	sibling_pid = -1;
	// create leaf
	if(level == treeHeight)
	{
		// read current leaf		
		BTLeafNode curLeaf;
		curLeaf.read(curPid,pf);
		// try inserting new data into leaf
		if(curLeaf.insert(key,rid) == 0) // successful
		{
			curLeaf.write(curPid,pf);
			return 0;
		}
		// otherwise split leaf
		// create newleaf for splitting
		BTLeafNode splitLeaf;
		int splitLeafKey;
		int lastPid = pf.endPid();
		if(curLeaf.insertAndSplit(key,rid,splitLeaf,splitLeafKey)<0)
			return -1;
		// splitLeafKey is the middle element that has to be copied up
		sibling_key = splitLeafKey;
		sibling_pid = lastPid;
		// set next node pointers for curLeaf and splitLeaf
		splitLeaf.setNextNodePtr(curLeaf.getNextNodePtr());
		curLeaf.setNextNodePtr(lastPid);
		if(splitLeaf.write(lastPid,pf)<0)
			return -1;
		if(curLeaf.write(curPid,pf)<0)
			return -1;
		if(treeHeight == 1)// first non-leaf node
		{
			BTNonLeafNode new_root;
			new_root.initializeRoot(curPid,key,lastPid);
			treeHeight++;
			// update rootPid
			rootPid = pf.endPid();
			new_root.write(rootPid,pf);

		}
		return 0;
	}
	else
	{
		// havent reached height yet! still in the middle of the tree
		BTNonLeafNode midNode;
		midNode.read(curPid,pf);

		// find child node for this key
		PageId childPid = -1;
		midNode.locateChildPtr(key,childPid);
		int insertKey = -1;
		PageId insertPid = -1;
		error = insert_recursive(key,rid,childPid,insertKey,insertPid,level+1);
		// check if we were able to successfully insert value
		if(insertKey!=-1 && insertPid!=-1)
		{
			if(midNode.insert(insertKey,insertPid) == 0)// success!
			{
				midNode.write(curPid,pf);
				return 0;
			}
		// else split and move key up
		BTNonLeafNode splitMidNode;
		int splitKey;
		midNode.insertAndSplit(insertKey,insertPid,splitMidNode,splitKey);
		int lastPid = pf.endPid();
		sibling_key = splitKey;
		sibling_pid = lastPid;
		if(splitMidNode.write(lastPid,pf)<0)
			return -1;
		if(midNode.write(curPid,pf)<0)
			return-1;
		if(treeHeight == 1)
		{
			BTNonLeafNode new_root;
			new_root.initializeRoot(curPid,splitKey,lastPid);
			treeHeight++;
			// update rootPid
			rootPid = pf.endPid();
			new_root.write(rootPid,pf);
		}

	}
		return 0;
	}
}


RC BTreeIndex::insert(int key, const RecordId& rid)
{
    PageId sibling_pid;
    int sibling_key;
    int level = 1;
    // new root
    if(treeHeight == 0)
    {
    	// create new leaf node to act as root
    	BTLeafNode root;
    	if(root.insert(key,rid) < 0)
    	{
    		// fprintf(stderr, "Error: Cannot create root!");
    		return -1;
    	}
    	// new file
    	// make rootPid start from 1 and let pid = 0 be for storing private data
    	if(pf.endPid()==0)
    	{
    		rootPid = 1;
    	}
    	else
    	{
    		rootPid = pf.endPid();
    	}
    	// increment height if insertion successful
    	treeHeight++;
    	// write leaf into specified pageid
    	return root.write(rootPid,pf);
    }
    return insert_recursive(key,rid,rootPid,sibling_pid,sibling_key,level);
}

/**
 * Run the standard B+Tree key search algorithm and identify the
 * leaf node where searchKey may exist. If an index entry with
 * searchKey exists in the leaf node, set IndexCursor to its location
 * (i.e., IndexCursor.pid = PageId of the leaf node, and
 * IndexCursor.eid = the searchKey index entry number.) and return 0.
 * If not, set IndexCursor.pid = PageId of the leaf node and
 * IndexCursor.eid = the index entry immediately after the largest
 * index key that is smaller than searchKey, and return the error
 * code RC_NO_SUCH_RECORD.
 * Using the returned "IndexCursor", you will have to call readForward()
 * to retrieve the actual (key, rid) pair from the index.
 * @param key[IN] the key to find
 * @param cursor[OUT] the cursor pointing to the index entry with
 *                    searchKey or immediately behind the largest key
 *                    smaller than searchKey.
 * @return 0 if searchKey is found. Othewise an error code
 */
RC BTreeIndex::locate(int searchKey, IndexCursor& cursor)
{
    return 0;
}

/*
 * Read the (key, rid) pair at the location specified by the index cursor,
 * and move foward the cursor to the next entry.
 * @param cursor[IN/OUT] the cursor pointing to an leaf-node index entry in the b+tree
 * @param key[OUT] the key stored at the index cursor location.
 * @param rid[OUT] the RecordId stored at the index cursor location.
 * @return error code. 0 if no error
 */
RC BTreeIndex::readForward(IndexCursor& cursor, int& key, RecordId& rid)
{
    return 0;
}
