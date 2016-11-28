#include "BTreeNode.h"
#include <iostream>

using namespace std;

//Leaf node constructor
BTLeafNode::BTLeafNode()
{
	number_keys=0;
	std::fill(buffer, buffer + PageFile::PAGE_SIZE, 0); //clear the buffer if necessary
}

/*
 * Read the content of the node from the page pid in the PageFile pf.
 * @param pid[IN] the PageId to read
 * @param pf[IN] PageFile to read from
 * @return 0 if successful. Return an error code if there is an error.
 */
RC BTLeafNode::read(PageId pid, const PageFile& pf)
{
	//Use PageFile to read from selected page into buffer
	return pf.read(pid, buffer);	
}
    
/*
 * Write the content of the node to the page pid in the PageFile pf.
 * @param pid[IN] the PageId to write to
 * @param pf[IN] PageFile to write to
 * @return 0 if successful. Return an error code if there is an error.
 */
RC BTLeafNode::write(PageId pid, PageFile& pf)
{
	//Use PageFile to write from buffer into selected page
	return pf.write(pid, buffer);
}

/*
 * Return the number of keys stored in the node.
 * @return the number of keys in the node
 */
 int BTLeafNode::maxKeyCount()
{
	int size_record = sizeof(int) + sizeof(RecordId);
	return (PageFile::PAGE_SIZE - sizeof(PageId))/size_record;
}
int BTLeafNode::getKeyCount()
{
	//return number_keys;
	
	//This is the size in bytes of an entry pair
	int size_record = sizeof(RecordId) + sizeof(int);
	int count=0;
	char* temp = buffer;
	
	//Loop through all the indexes in the temp buffer; increment by 12 bytes to jump to next key
	//1008 is the largest possible index of the next inserted pair (since we already know we can fit another pair)
	int i;
	for(i=0; i<=1008; i+=size_record)
	{
		int innerKey;
		memcpy(&innerKey, temp, sizeof(int)); //Save the current key inside buffer as innerKey
		if(innerKey==0) //Once we hit key of 0, we break
			break;
		//Otherwise, increment count
		count++;
		
		temp += size_record; //Jump temp over to the next key
	}
	
	return count;
}

/*
 * Insert a (key, rid) pair to the node.
 * @param key[IN] the key to insert
 * @param rid[IN] the RecordId to insert
 * @return 0 if successful. Return an error code if the node is full.
 */
RC BTLeafNode::insert(int key, const RecordId& rid)
{
	//Save last 4 bytes (the pid) for reconstructing the inserted leaf
	PageId nextNodePtr = getNextNodePtr();
	
	int size_record = sizeof(RecordId) + sizeof(int);
	
	int totPairs = maxKeyCount();
	if(getKeyCount()+1 > totPairs) //if(getKeyCount()+1 > 85)
	{
		return RC_NODE_FULL;
	}
	
	//Now we must go through the buffer's sorted keys to see where the new key goes
	char* temp = buffer;
	
	
	int i;
	for(i=0; i<1008; i+=size_record)
	{
		int innerKey;
		memcpy(&innerKey, temp, sizeof(int)); //Save the current key inside buffer as innerKey
		
		//Once the innerKey is null or key is smaller than some inside key, we stop
		if(innerKey==0 || !(key > innerKey))
			break;
		
		temp += size_record; //Jump temp over to the next key
	}
	
	//At this point, variable i holds the index to insert the pair and temp is the buffer at that index
	char* buffer1 = (char*)malloc(PageFile::PAGE_SIZE);
	std::fill(buffer1, buffer1 + PageFile::PAGE_SIZE, 0); //clear the buffer if necessary
	
	//Copy all values from buffer into buffer1 up until i
	memcpy(buffer1, buffer, i);
	
	//Values to insert as new (key, rid) pair
	PageId pid = rid.pid;
	int sid = rid.sid;
	
	memcpy(buffer1+i, &key, sizeof(int));
	memcpy(buffer1+i+sizeof(int), &rid, sizeof(RecordId));
	
	//INCREMENTAL POINTER METHOD:
	//char* insertPos = buffer1+i;
	//memcpy(insertPos, &pid, sizeof(PageId));
	//insertPos += sizeof(PageId);
	//memcpy(insertPos, &sid, sizeof(int));
	//insertPos += sizeof(int);
	//memcpy(insertPos, &key, sizeof(int));
	
	//Copy the rest of the values into buffer1
	//Notice that we are neglecting nextNodePtr, so we'll manually copy that in
	memcpy(buffer1+i+size_record, buffer+i, getKeyCount()*size_record - i);
	memcpy(buffer1+PageFile::PAGE_SIZE-sizeof(PageId), &nextNodePtr, sizeof(PageId));
	
	//Copy buffer1 into buffer, then delete temporary buffer1 to prevent memory leak
	memcpy(buffer, buffer1, PageFile::PAGE_SIZE);
	free(buffer1);
	
	//Successfully inserted leaf node, so we increment number of keys
	number_keys++;
	
	return 0;
}

/*
 * Insert the (key, rid) pair to the node
 * and split the node half and half with sibling.
 * The first key of the sibling node is returned in siblingKey.
 * @param key[IN] the key to insert.
 * @param rid[IN] the RecordId to insert.
 * @param sibling[IN] the sibling node to split with. This node MUST be EMPTY when this function is called.
 * @param siblingKey[OUT] the first key in the sibling node after split.
 * @return 0 if successful. Return an error code if there is an error.
 */
RC BTLeafNode::insertAndSplit(int key, const RecordId& rid, 
                              BTLeafNode& sibling, int& siblingKey)
{
	//Save last 4 bytes (the pid) for reconstructing the inserted leaf
	PageId nextNodePtr = getNextNodePtr();
	
	int size_record = sizeof(RecordId) + sizeof(int);
	int totPairs = maxKeyCount();
	
	//Only split if inserting will cause an overflow; otherwise, return error
	if(!(getKeyCount() >= totPairs))
		return RC_INVALID_FILE_FORMAT;
	
	//If sibling node is not empty, return error
	if(sibling.getKeyCount()!=0)
		return RC_INVALID_ATTRIBUTE;
	
	//Clear sibling buffer just in case
	std::fill(sibling.buffer, sibling.buffer + PageFile::PAGE_SIZE, 0); //clear the buffer if necessary
	
	//Calculate keys to remain in the first half
	int numHalfKeys = ((int)((getKeyCount()+1)/2));
	
	//Find the index at which to split the node's buffer
	int halfIndex = numHalfKeys*size_record;
	
	//Copy everything on the right side of halfIndex into sibling's buffer (ignore the pid)
	memcpy(sibling.buffer, buffer+halfIndex, PageFile::PAGE_SIZE-sizeof(PageId)-halfIndex);
	
	//Update sibling's number of keys and set pid to current node's pid ptr
	sibling.number_keys = getKeyCount() - numHalfKeys;
	sibling.setNextNodePtr(getNextNodePtr());
	
	//Clear the second half of current buffer except for pid; update number of keys
	std::fill(buffer+halfIndex, buffer + PageFile::PAGE_SIZE - sizeof(PageId), 0); 
	number_keys = numHalfKeys;
	
	//Check which buffer to insert new (key, rid) into
	int firstHalfKey;
	memcpy(&firstHalfKey, sibling.buffer, sizeof(int));
	
	//Insert pair and increment number of keys
	if(key>=firstHalfKey) //If our key belongs in the second buffer (since it's sorted)...
	{
		sibling.insert(key, rid);
	}
	else //Otherwise, place it in the first half
	{
		insert(key, rid);
	}
	
	//Copy over sibling's first key and rid
	memcpy(&siblingKey, sibling.buffer, sizeof(int));
	
	RecordId siblingRid;
	siblingRid.pid = -1;
	siblingRid.sid = -1;
	memcpy(&siblingRid, sibling.buffer+sizeof(int), sizeof(RecordId));
	
	//Remember not to touch the next node pointer
	//Since we use it later, changing this will destroy the index tree's leaf node mapping
	
	return 0;
}
/*
 * Find the entry whose key value is larger than or equal to searchKey
 * and output the eid (entry number) whose key value >= searchKey.
 * Remeber that all keys inside a B+tree node should be kept sorted.
 * @param searchKey[IN] the key to search for
 * @param eid[OUT] the entry number that contains a key larger than or equalty to searchKey
 * @return 0 if successful. Return an error code if there is an error.
 */
RC BTLeafNode::locate(int searchKey, int& eid)
{
	//This is the size in bytes of an entry pair
	int size_record = sizeof(RecordId) + sizeof(int);
	
	char* temp = buffer;
	
	//Loop through all the indexes in the temp buffer; increment by 12 bytes to jump to next key	
	int i;
	for(i=0; i<getKeyCount()*size_record; i+=size_record)
	{
		int innerKey;
		memcpy(&innerKey, temp, sizeof(int)); //Save the current key inside buffer as innerKey
		
		//Once innerKey is larger than or equal to searchKey
		if(innerKey >= searchKey)
		{
			//Set eid to the current byte index divided by size of a pair entry
			//This effectively produces eid
			eid = i/size_record;
			return 0;
		}
		
		temp += size_record; //Jump temp over to the next key
	}
	
	//If we get here, all of the keys inside the buffer were less than searchKey
	eid = getKeyCount();
	return 0;
}

/*
 * Read the (key, rid) pair from the eid entry.
 * @param eid[IN] the entry number to read the (key, rid) pair from
 * @param key[OUT] the key from the entry
 * @param rid[OUT] the RecordId from the entry
 * @return 0 if successful. Return an error code if there is an error.
 */
RC BTLeafNode::readEntry(int eid, int& key, RecordId& rid)
{
	//This is the size in bytes of an entry pair
	int size_record = sizeof(RecordId) + sizeof(int);
	
	//If eid is out of bounds (negative or more than the number of keys we have), return error
	if(eid >= getKeyCount() || eid < 0)
		return RC_NO_SUCH_RECORD; //Not sure which error to return...RC_INVALID_CURSOR?
	
	//This is the position in bytes of the entry
	int bytePos = eid*size_record;
	
	char* temp = buffer;
	
	//Copy the data into parameters
	memcpy(&key, temp+bytePos, sizeof(int));
	memcpy(&rid, temp+bytePos+sizeof(int), sizeof(RecordId));
	
	return 0;
}

/*
 * Return the pid of the next slibling node.
 * @return the PageId of the next sibling node 
 */
PageId BTLeafNode::getNextNodePtr()
{
	//Initialize a PageId; assume there's no next node by default
	PageId pid = 0; 
	char* temp = buffer;
	
	//Find the last PageId section of the buffer and copy data over to pid
	memcpy(&pid, temp+PageFile::PAGE_SIZE-sizeof(PageId), sizeof(PageId));
	
	return pid;
}

/*
 * Set the pid of the next slibling node.
 * @param pid[IN] the PageId of the next sibling node 
 * @return 0 if successful. Return an error code if there is an error.
 */
RC BTLeafNode::setNextNodePtr(PageId pid)
{
	//If pid is invalid, return error
	if(pid < 0)
		return RC_INVALID_PID;
	
	char* temp = buffer;
	
	//Otherwise, copy the parameter pid into our buffer
	memcpy(temp+PageFile::PAGE_SIZE-sizeof(PageId), &pid, sizeof(PageId));
	
	return 0;
}

/*
 * Print the keys of the node to cout
 */
void BTLeafNode::print()
{
	//This is the size in bytes of an entry pair
	int size_record = sizeof(RecordId) + sizeof(int);
	
	char* temp = buffer;
	
	for(int i=0; i<getKeyCount()*size_record; i+=size_record)
	{
		int innerKey;
		memcpy(&innerKey, temp, sizeof(int)); //Save the current key inside buffer as innerKey
		
		cout << innerKey << " ";
		
		temp += size_record; //Jump temp over to the next key
	}
	
	cout << "" << endl;
}

//----------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------


//Nonleaf node constructor
BTNonLeafNode::BTNonLeafNode()
{
	number_keys=0;
	std::fill(buffer, buffer + PageFile::PAGE_SIZE, 0); //clear the buffer if necessary
}

/*
 * Read the content of the node from the page pid in the PageFile pf.
 * @param pid[IN] the PageId to read
 * @param pf[IN] PageFile to read from
 * @return 0 if successful. Return an error code if there is an error.
 */
RC BTNonLeafNode::read(PageId pid, const PageFile& pf)
{
	//Use PageFile to read from selected page into buffer
	return pf.read(pid, buffer);
}
    
/*
 * Write the content of the node to the page pid in the PageFile pf.
 * @param pid[IN] the PageId to write to
 * @param pf[IN] PageFile to write to
 * @return 0 if successful. Return an error code if there is an error.
 */
RC BTNonLeafNode::write(PageId pid, PageFile& pf)
{
	//Use PageFile to write from buffer into selected page
	return pf.write(pid, buffer);
}

/*
 * Return the number of keys stored in the node.
 * @return the number of keys in the node
 */
int BTNonLeafNode::maxKeyCount()
{
	int size_record = sizeof(int) + sizeof(PageId);
	return (PageFile::PAGE_SIZE - sizeof(PageId))/size_record;
}
int BTNonLeafNode::getKeyCount()
{
	//return number_keys;
	
	//This is the size in bytes of an entry pair
	int size_record = sizeof(PageId) + sizeof(int);
	int totPairs = maxKeyCount(); //127
	int count=0;
	//Now we must go through the buffer's sorted keys to see where the new key goes
	//For nonleaf nodes only, remember to skip the first 8 bytes (4 bytes pid, 4 bytes empty)
	char* temp = buffer+8;
	
	//Loop through all the indexes in the temp buffer; increment by 8 bytes to jump to next key
	//1016 is the largest possible index of the next inserted pair (since we already know we can fit another pair)
	//For nonleaf nodes only, remember that we start the (key,pid) entries at index 8
	int i;
	for(i=8; i<=1016; i+=size_record)
	{
		int innerKey;
		memcpy(&innerKey, temp, sizeof(int)); //Save the current key inside buffer as innerKey
		if(innerKey==0) //Once we hit key of 0, we break
			break;
		//Otherwise, increment count
		count++;

		temp += size_record; //Jump temp over to the next key
	}
	
	return count;
}

/*
 * Insert a (key, pid) pair to the node.
 * @param key[IN] the key to insert
 * @param pid[IN] the PageId to insert
 * @return 0 if successful. Return an error code if the node is full.
 */
RC BTNonLeafNode::insert(int key, PageId pid)
{
	//Nonleaf nodes have pairs of integer keys and PageIds, with another PageId at the front
	int size_record = sizeof(PageId) + sizeof(int);
	int totPairs = maxKeyCount(); //127
	
	//Return error if no more space in this node
	//Page has 1024 bytes, we need to store 8 bytes (key, pid)
	//That means we can fit 127 with 8 bytes left over; the first 4 will be used for pid
	//Check if adding one more (key, pid) pair will exceed the size limit of 127
	if(getKeyCount()+1 > totPairs)
	{
		//cout << "Cannot insert anymore: this node is full!" << endl;
		return RC_NODE_FULL;
	}

	//Now we must go through the buffer's sorted keys to see where the new key goes
	//For nonleaf nodes only, remember to skip the first 8 bytes (4 bytes pid, 4 bytes empty)
	char* temp = buffer+8;
	
	//Loop through all the indexes in the temp buffer; increment by 8 bytes to jump to next key
	//1016 is the largest possible index of the next inserted pair (since we already know we can fit another pair)
	//For nonleaf nodes only, remember that we start the (key,pid) entries at index 8
	int i;
	for(i=8; i<1016; i+=size_record)
	{
		int innerKey;
		memcpy(&innerKey, temp, sizeof(int)); //Save the current key inside buffer as innerKey
		
		//Once the innerKey is null or key is smaller than some inside key, we stop
		if(innerKey==0 || !(key > innerKey))
			break;
		
		temp += size_record; //Jump temp over to the next key
	}
	
	//At this point, variable i holds the index to insert the pair and temp is the buffer at that index
	char* buffer1 = (char*)malloc(PageFile::PAGE_SIZE);
	std::fill(buffer1, buffer1 + PageFile::PAGE_SIZE, 0); //clear the buffer if necessary
	
	//Copy all values from buffer into buffer1 up until i
	memcpy(buffer1, buffer, i);
	
	//Copy key and pid into buffer1
	memcpy(buffer1+i, &key, sizeof(int));
	memcpy(buffer1+i+sizeof(int), &pid, sizeof(PageId));
	
	//Copy the rest of the values into buffer1
	//For nonleaf nodes only, remember that we must add in 8 bytes extra
	//Otherwise we would be counting the initial (pid, empty) as a key
	memcpy(buffer1+i+size_record, buffer+i, getKeyCount()*size_record - i + 8);
	
	
	//Copy buffer1 into buffer, then delete temporary buffer1 to prevent memory leak
	memcpy(buffer, buffer1, PageFile::PAGE_SIZE);
	free(buffer1);
	
	//Successfully inserted leaf node, so we increment number of keys
	number_keys++;	
	return 0;
}

/*
 * Insert the (key, pid) pair to the node
 * and split the node half and half with sibling.
 * The middle key after the split is returned in midKey.
 * @param key[IN] the key to insert
 * @param pid[IN] the PageId to insert
 * @param sibling[IN] the sibling node to split with. This node MUST be empty when this function is called.
 * @param midKey[OUT] the key in the middle after the split. This key should be inserted to the parent node.
 * @return 0 if successful. Return an error code if there is an error.
 */
RC BTNonLeafNode::insertAndSplit(int key, PageId pid, BTNonLeafNode& sibling, int& midKey)
{
	//Nonleaf nodes have pairs of integer keys and PageIds, with another PageId at the front
	int size_record = sizeof(PageId) + sizeof(int);
	int totPairs = maxKeyCount(); //127
	
	//Only split if inserting will cause an overflow; otherwise, return error
	if(!(getKeyCount() >= totPairs))
		return RC_INVALID_FILE_FORMAT;
	
	//If sibling node is not empty, return error
	if(sibling.getKeyCount()!=0)
		return RC_INVALID_ATTRIBUTE;

	//Clear sibling buffer just in case
	std::fill(sibling.buffer, sibling.buffer + PageFile::PAGE_SIZE, 0); //clear the buffer if necessary

	//Calculate keys to remain in the first half
	int numHalfKeys = ((int)((getKeyCount()+1)/2));

	//Find the index at which to split the node's buffer
	//For nonleaf nodes only, remember to add an offset of 8 for initial pid
	int halfIndex = numHalfKeys*size_record + 8;
	
	//REMOVING THE MEDIAN KEY
	
	//Find the last key of the first half and the first key of the second half
	int key1 = -1;
	int key2 = -1;
	
	memcpy(&key1, buffer+halfIndex-8, sizeof(int));
	memcpy(&key2, buffer+halfIndex, sizeof(int));
	
	if(key < key1) //key1 is the median key to be removed
	{
		//Copy everything on the right side of halfIndex into sibling's buffer (ignore the pid)
		//For nonleaf nodes only, remember to add an offset of 8 for initial pid
		memcpy(sibling.buffer+8, buffer+halfIndex, PageFile::PAGE_SIZE-halfIndex);
		//Update sibling's number of keys
		sibling.number_keys = getKeyCount() - numHalfKeys;
		
		//Copy down the median key before getting rid of it in buffer
		memcpy(&midKey, buffer+halfIndex-8, sizeof(int));
		
		//Also set the sibling pid from buffer before getting rid of it
		memcpy(sibling.buffer, buffer+halfIndex-4, sizeof(PageId));
		
		//Clear the second half of current buffer; update number of keys
		//Also clear the last key of the first half, since this is the median key
		std::fill(buffer+halfIndex-8, buffer + PageFile::PAGE_SIZE, 0); 
		number_keys = numHalfKeys - 1;
		
		//Insert the (key, pid) pair into buffer, since it's key is smaller than the median
		insert(key, pid);		
	}
	else if(key > key2) //key2 is the median key to be removed
	{
		//Copy everything on the right side of halfIndex EXCEPT FOR THE FIRST KEY into sibling's buffer (ignore the pid)
		//The first key on the right side here is the median key, which will be removed
		//For nonleaf nodes only, remember to add an offset of 8 for initial pid
		memcpy(sibling.buffer+8, buffer+halfIndex+8, PageFile::PAGE_SIZE-halfIndex-8);
		//Update sibling's number of keys
		sibling.number_keys = getKeyCount() - numHalfKeys - 1;
		
		//Copy down the median key before getting rid of it in buffer
		memcpy(&midKey, buffer+halfIndex, sizeof(int));
		
		//Also set the sibling pid from buffer before getting rid of it
		memcpy(sibling.buffer, buffer+halfIndex+4, sizeof(PageId));
		
		//Clear the second half of current buffer; update number of keys
		std::fill(buffer+halfIndex, buffer + PageFile::PAGE_SIZE, 0); 
		number_keys = numHalfKeys;
		
		//Insert the (key, pid) pair into sibling, since it's key is larger than the median
		sibling.insert(key, pid);
		
	}
	else //key is the median key to be removed
	{
		//Copy everything on the right side of halfIndex into sibling's buffer (ignore the pid)
		//For nonleaf nodes only, remember to add an offset of 8 for initial pid
		memcpy(sibling.buffer+8, buffer+halfIndex, PageFile::PAGE_SIZE-halfIndex);
		//Update sibling's number of keys
		sibling.number_keys = getKeyCount() - numHalfKeys;
		
		//Clear the second half of current buffer; update number of keys
		std::fill(buffer+halfIndex, buffer + PageFile::PAGE_SIZE, 0); 
		number_keys = numHalfKeys;
		
		//The key we're inserting IS the median key, so we stop the insertion process and return
		midKey = key;
		
		//Set the sibling pid from the median key parameter
		memcpy(sibling.buffer, &pid, sizeof(PageId));
	}

	//If we reach this, then we have somehow split the node and inserted the (key, pid) pair
	return 0;
}

/*
 * Given the searchKey, find the child-node pointer to follow and
 * output it in pid.
 * @param searchKey[IN] the searchKey that is being looked up.
 * @param pid[OUT] the pointer to the child node to follow.
 * @return 0 if successful. Return an error code if there is an error.
 */
RC BTNonLeafNode::locateChildPtr(int searchKey, PageId& pid)
{
	//This is the size in bytes of an entry pair
	int size_record = sizeof(PageId) + sizeof(int);
	
	//Skip the first 8 offset bytes, since there's no key there
	char* temp = buffer+8;	
	
	//Loop through all the indexes in the temp buffer; increment by 8 bytes to jump to next key	
	int i;	
	for(i=8; i<getKeyCount()*size_record+8; i+=size_record)
	{
		int innerKey;
		memcpy(&innerKey, temp, sizeof(int)); //Save the current key inside buffer as innerKey
				
		if(i==8 && innerKey > searchKey) //If searchKey is less than first key, we need to return initial pid
		{
			//A special check is necessarily since the initial pid is in a different buffer position from the rest
			memcpy(&pid, buffer, sizeof(PageId));
			return 0;
		}
		else if(innerKey > searchKey)
		{
			//Set pid to be the left pid (that is, the pid on the small side of innerKey)
			memcpy(&pid, temp-4, sizeof(PageId));
			return 0;
		}
		
		//Otherwise, searchKey is greater than or equal to innerKey, so we keep checking
		temp += size_record; //Jump temp over to the next key
		
	}
	
	//If we get here, searchKey was greater than all instances of innerKey
	//Copy over the last, right-most pid before returning
	//Remember that temp is now on the next non-existent node's position, so we need to decrement by 4 bytes
	memcpy(&pid, temp-4, sizeof(PageId));
	return 0;
}

/*
 * Initialize the root node with (pid1, key, pid2).
 * @param pid1[IN] the first PageId to insert
 * @param key[IN] the key that should be inserted between the two PageIds
 * @param pid2[IN] the PageId to insert behind the key
 * @return 0 if successful. Return an error code if there is an error.
 */
RC BTNonLeafNode::initializeRoot(PageId pid1, int key, PageId pid2)
{
	RC error;

	std::fill(buffer, buffer + PageFile::PAGE_SIZE, 0); //clear the buffer if necessary
	
	//This time, don't skip the first 8 offset bytes
	//We're actually initializing it to something explicitly
	char* temp = buffer;
	
	//Copy over the initial pid into buffer
	memcpy(temp, &pid1, sizeof(PageId));
	
	//Copy the first pair into buffer
	//memcpy(temp+8, &key, sizeof(int));
	//memcpy(temp+12, &pid2, sizeof(PageId));
	error = insert(key, pid2);
	
	if(error!=0)
		return error;
	
	//Set number of (key, pid) pairs to 1
	//Only need this if we dont use insert to set (key, pid2) pair
	//number_keys = 1;
	
	return 0;
}

/*
 * Print the keys of the node to cout
 */
void BTNonLeafNode::print()
{
	//This is the size in bytes of an entry pair
	int size_record = sizeof(PageId) + sizeof(int);
	
	//Skip the first 8 offset bytes, since there's no key there
	char* temp = buffer+8;
	
	for(int i=8; i<getKeyCount()*size_record+8; i+=size_record)
	{
		int innerKey;
		memcpy(&innerKey, temp, sizeof(int)); //Save the current key inside buffer as innerKey

		cout << innerKey << " ";
		
		//Otherwise, searchKey is greater than or equal to innerKey, so we keep checking
		temp += size_record; //Jump temp over to the next key
	}
	
	cout << "" << endl;	
}