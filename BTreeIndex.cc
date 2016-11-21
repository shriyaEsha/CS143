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
#include <iostream>

using namespace std;

/*
 * BTreeIndex constructor
 */
BTreeIndex::BTreeIndex()
{
    rootPid = -1;
    //  tree height is 0 on creation
	treeHeight = 0; 
	
	std::fill(buffer, buffer + PageFile::PAGE_SIZE, 0); // clear the buffer if necessary
}

/*
 * Open the index file in read or write mode.
 * Under 'w' mode, the index file should be created if it does not exist.
 * @param indexname[IN] the name of the index file
 * @param mode[IN] 'r' for read, 'w' for write
 * @return rc code. 0 if no rc
 */
RC BTreeIndex::open(const string& indexname, char mode)
{
    RC rc = pf.open(indexname, mode);

	if(rc!=0)
		return rc;
	
	if(pf.endPid()==0)
	{
		rootPid = -1;
		treeHeight = 0;
		RC rc = pf.write(0, buffer);
		if(rc!=0)
			return rc;
		return 0;
	}
	
	//  read current Pid and height from metadata
	rc = pf.read(0, buffer);

	if(rc!=0)
		return rc;
	
	int currPid;
	int currHeight;
	memcpy(&currPid, buffer, sizeof(int));
	memcpy(&currHeight, buffer+4, sizeof(int));
	
	//  check if saved pid and height are valid and assign them	
	if(currPid>0 && currHeight>=0)
	{
		rootPid = currPid;
		treeHeight = currHeight;
	}
	
	return 0;
}

/*
 * Close the index file.
 * @return rc code. 0 if no rc
 */
RC BTreeIndex::close()
{
	//  Save pid and height to PageFile
	memcpy(buffer, &rootPid, sizeof(int));
	memcpy(buffer+4, &treeHeight, sizeof(int));
	
	RC rc = pf.write(0, buffer);
	
	if(rc!=0)
		return rc;

    return pf.close();
}

/*
 * Insert (key, RecordId) pair to the index.
 * @param key[IN] the key for the value inserted into the index
 * @param rid[IN] the RecordId for the record being inserted into the index
 * @return rc code. 0 if no rc
 */
RC BTreeIndex::insert(int key, const RecordId& rid)
{	
	RC rc;

	//  new Tree!
	if(treeHeight==0)
	{
		//  new Leaf 
		BTLeafNode newRoot;
		newRoot.insert(key, rid);
		
		//  rootPid starts from 1 as 0 is for storing metadata
		if(pf.endPid()==0)
			rootPid = 1;
		else
			rootPid = pf.endPid();
		
		treeHeight++;
		//  write tree into PageFile		
		return newRoot.write(rootPid, pf);
	}
	
	int insertKey = -1;
	PageId insertPid = -1;
	
	rc = insert_recursive(key, rid, 1, rootPid, insertKey, insertPid);
	
	if(rc!=0)
		return rc;
	
	return 0;
}

RC BTreeIndex::insert_recursive(int key, const RecordId& rid, int currHeight, PageId currPid, int& tempKey, PageId& tempPid)
{
	RC rc;
	
	//  used for splitting	
	tempKey = -1;
	tempPid = -1;
	
	if(currHeight==treeHeight)
	{
		//  Read contents od current Leaf
		BTLeafNode currLeaf;
		currLeaf.read(currPid, pf);

		//  insert key into current leaf
		if(currLeaf.insert(key, rid)==0)
		{	
			currLeaf.write(currPid, pf);
			return 0;
		}

		//  overflow in leaf - split
		BTLeafNode newleaf;
		int newkey;
		rc = currLeaf.insertAndSplit(key, rid, newleaf, newkey);
		
		if(rc!=0)
			return rc;
		
		int lastPid = pf.endPid();
		tempKey = newkey;
		tempPid = lastPid;

		//  setting next node pointers for currLeaf and newleaf
		newleaf.setNextNodePtr(currLeaf.getNextNodePtr());
		currLeaf.setNextNodePtr(lastPid);

		//  newLeaf - begin writing from end
		rc = newleaf.write(lastPid, pf);
		
		if(rc!=0)
			return rc;
		
		rc = currLeaf.write(currPid, pf);
		
		if(rc!=0)
			return rc;
		
		//  have only root, must insert next level of nonLeaf nodes
		if(treeHeight==1)
		{
			BTNonLeafNode newRoot;
			newRoot.initializeRoot(currPid, newkey, lastPid);
			treeHeight++;
			
			rootPid = pf.endPid();
			newRoot.write(rootPid, pf);
		}
		
		return 0;
	}
	else
	{
		//  still in the middle of tree
		BTNonLeafNode midNode;
		midNode.read(currPid, pf);
		
		PageId childPid = -1;
		midNode.locateChildPtr(key, childPid);
		
		int insertKey = -1;
		PageId insertPid = -1;
		
		rc = insert_recursive(key, rid, currHeight+1, childPid, insertKey, insertPid);
		
		//  overflow! 		
		if(!(insertKey==-1 && insertPid==-1)) 
		{
			if(midNode.insert(insertKey, insertPid)==0)
			{
				// If we were able to successfully insert the child's median key into midNode
				// Write it into PageFile
				midNode.write(currPid, pf);
				return 0;
			}
			//  must split midNode due to overflow	
			BTNonLeafNode anotherMidNode;
			int newkey;
			
			midNode.insertAndSplit(insertKey, insertPid, anotherMidNode, newkey);
			
			int lastPid = pf.endPid();
			tempKey = newkey;
			tempPid = lastPid;
			
			// / write contents into midNode after split
			rc = midNode.write(currPid, pf);
			
			if(rc!=0)
				return rc;
			
			rc = anotherMidNode.write(lastPid, pf);
			
			if(rc!=0)
				return rc;
			
			// If we just split a root, we'll now need a new single non-leaf node
			// The new first value of the sibling node (anotherMidNode) gets inserted into root
			if(treeHeight==1)
			{
				//  init root with pointers to children nodes and update height
				BTNonLeafNode newRoot;
				newRoot.initializeRoot(currPid, newkey, lastPid);
				treeHeight++;
				
				//  update rootPid and write to PageFile
				rootPid = pf.endPid();
				newRoot.write(rootPid, pf);
			}
			
		}
		return 0;
	}
}

/*
 * Find the leaf-node index entry whose key value is larger than or 
 * equal to searchKey, and output the location of the entry in IndexCursor.
 * IndexCursor is a "pointer" to a B+tree leaf-node entry consisting of
 * the PageId of the node and the SlotID of the index entry.
 * Note that, for range queries, we need to scan the B+tree leaf nodes.
 * For example, if the query is "key > 1000", we should scan the leaf
 * nodes starting with the key value 1000. For this reason,
 * it is better to return the location of the leaf node entry 
 * for a given searchKey, instead of returning the RecordId
 * associated with the searchKey directly.
 * Once the location of the index entry is identified and returned 
 * from this function, you should call readForward() to retrieve the
 * actual (key, rid) pair from the index.
 * @param key[IN] the key to find.
 * @param cursor[OUT] the cursor pointing to the first index entry
 *                    with the key value.
 * @return rc code. 0 if no rc.
 */
RC BTreeIndex::locate(int searchKey, IndexCursor& cursor)
{
	RC rc;	
	BTNonLeafNode midNode;
	BTLeafNode leaf;
	
	int eid;
	int currHeight = 1;
	PageId nextPid = rootPid;
	
	while(currHeight!=treeHeight)
	{
		rc = midNode.read(nextPid, pf);
		
		if(rc!=0)
			return rc;
		
		//  Locate child node to look at next given the search key and update nextPid
		rc = midNode.locateChildPtr(searchKey, nextPid);
		
		if(rc!=0)
			return rc;
		
		currHeight++;
	}
	
	rc = leaf.read(nextPid, pf);
		
	if(rc!=0)
		return rc;
	
	// Locate leaf node that contains searchKey and update eid
	rc = leaf.locate(searchKey, eid);
	
	if(rc!=0)
		return rc;
	
	// Update indexCursor
	cursor.eid = eid;
	cursor.pid = nextPid;
	
	return 0;
}


/*
 * Read the (key, rid) pair at the location specified by the index cursor,
 * and move foward the cursor to the next entry.
 * @param cursor[IN/OUT] the cursor pointing to an leaf-node index entry in the b+tree
 * @param key[OUT] the key stored at the index cursor location.
 * @param rid[OUT] the RecordId stored at the index cursor location.
 * @return rc code. 0 if no rc
 */ 
RC BTreeIndex::readForward(IndexCursor& cursor, int& key, RecordId& rid)
{
	RC rc;

	PageId cursorPid = cursor.pid;
	int cursorEid = cursor.eid;
	
	// Load data for the cursor's leaf
	BTLeafNode leaf;
	rc = leaf.read(cursorPid, pf);
	
	if(rc!=0)
		return rc;
	
	// Based on the cursor's eid, find return the key and rid
	rc = leaf.readEntry(cursorEid, key, rid);
	
	if(rc!=0)
		return rc;
		
	// the cursor's PageId should never go beyond an uninitialized page
	if(cursorPid <= 0)
		return RC_INVALID_CURSOR;
	
	// cursorEid should not exceed keys in leafNode
	if(cursorEid+1 >= leaf.getKeyCount())
	{
		cursorEid = 0;
		cursorPid = leaf.getNextNodePtr();
	}
	else
		cursorEid++;
	
	// Write into cursor
	cursor.eid = cursorEid;
	cursor.pid = cursorPid;
	return 0;
}

PageId BTreeIndex::getRootPid()
{
	return rootPid;
}

int BTreeIndex::getTreeHeight()
{
	return treeHeight;
}

