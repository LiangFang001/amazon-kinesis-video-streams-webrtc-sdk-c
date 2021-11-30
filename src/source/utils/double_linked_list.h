/*
 * Copyright 2021 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 *  http://aws.amazon.com/apache2.0
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */
#ifndef __AWS_KVS_WEBRTC_DOUBLE_LINKED_LIST_INCLUDE__
#define __AWS_KVS_WEBRTC_DOUBLE_LINKED_LIST_INCLUDE__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Double-linked list definition
 */
typedef struct __DoubleListNode {
    struct __DoubleListNode* pNext;
    struct __DoubleListNode* pPrev;
    UINT64 data;
} DoubleListNode, *PDoubleListNode;

typedef struct {
    UINT32 count;
    PDoubleListNode pHead;
    PDoubleListNode pTail;
} DoubleList, *PDoubleList;

/**
 * Internal Double Linked List operations
 */
STATUS doubleListAllocNode(UINT64, PDoubleListNode*);
STATUS doubleListInsertNodeHeadInternal(PDoubleList, PDoubleListNode);
STATUS doubleListInsertNodeTailInternal(PDoubleList, PDoubleListNode);
STATUS doubleListInsertNodeBeforeInternal(PDoubleList, PDoubleListNode, PDoubleListNode);
STATUS doubleListInsertNodeAfterInternal(PDoubleList, PDoubleListNode, PDoubleListNode);
STATUS doubleListRemoveNodeInternal(PDoubleList, PDoubleListNode);
STATUS doubleListGetNodeAtInternal(PDoubleList, UINT32, PDoubleListNode*);

//////////////////////////////////////////////////////////////////////////////////////////////////////
// Double-linked list functionality
//////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Create a new double linked list
 */
STATUS doubleListCreate(PDoubleList*);

/**
 * Frees a double linked list and deallocates the nodes
 */
STATUS doubleListFree(PDoubleList);

/**
 * Clears and deallocates all the items
 */
STATUS doubleListClear(PDoubleList, BOOL);

/**
 * Insert a node in the head position in the list
 */
STATUS doubleListInsertNodeHead(PDoubleList, PDoubleListNode);

/**
 * Insert a new node with the data at the head position in the list
 */
STATUS doubleListInsertItemHead(PDoubleList, UINT64);

/**
 * Insert a node in the tail position in the list
 */
STATUS doubleListInsertNodeTail(PDoubleList, PDoubleListNode);

/**
 * Insert a new node with the data at the tail position in the list
 */
STATUS doubleListInsertItemTail(PDoubleList, UINT64);

/**
 * Insert a node before a given node
 */
STATUS doubleListInsertNodeBefore(PDoubleList, PDoubleListNode, PDoubleListNode);

/**
 * Insert a new node with the data before a given node
 */
STATUS doubleListInsertItemBefore(PDoubleList, PDoubleListNode, UINT64);

/**
 * Insert a node after a given node
 */
STATUS doubleListInsertNodeAfter(PDoubleList, PDoubleListNode, PDoubleListNode);

/**
 * Insert a new node with the data after a given node
 */
STATUS doubleListInsertItemAfter(PDoubleList, PDoubleListNode, UINT64);

/**
 * Removes and deletes the head
 */
STATUS doubleListDeleteHead(PDoubleList);

/**
 * Removes and deletes the tail
 */
STATUS doubleListDeleteTail(PDoubleList);

/**
 * Removes the specified node
 */
STATUS doubleListRemoveNode(PDoubleList, PDoubleListNode);

/**
 * Removes and deletes the specified node
 */
STATUS doubleListDeleteNode(PDoubleList, PDoubleListNode);

/**
 * Gets the head node
 */
STATUS doubleListGetHeadNode(PDoubleList, PDoubleListNode*);

/**
 * Gets the tail node
 */
STATUS doubleListGetTailNode(PDoubleList, PDoubleListNode*);

/**
 * Gets the node at the specified index
 */
STATUS doubleListGetNodeAt(PDoubleList, UINT32, PDoubleListNode*);

/**
 * Gets the node data at the specified index
 */
STATUS doubleListGetNodeDataAt(PDoubleList, UINT32, PUINT64);

/**
 * Gets the node data
 */
STATUS doubleListGetNodeData(PDoubleListNode, PUINT64);

/**
 * Gets the next node
 */
STATUS doubleListGetNextNode(PDoubleListNode, PDoubleListNode*);

/**
 * Gets the previous node
 */
STATUS doubleListGetPrevNode(PDoubleListNode, PDoubleListNode*);

/**
 * Gets the count of nodes in the list
 */
STATUS doubleListGetNodeCount(PDoubleList, PUINT32);

/**
 * Append a double list to the other and then free the list being appended
 */
STATUS doubleListAppendList(PDoubleList, PDoubleList*);
#ifdef __cplusplus
}
#endif
#endif /* __AWS_KVS_WEBRTC_DOUBLE_LINKED_LIST_INCLUDE__ */
