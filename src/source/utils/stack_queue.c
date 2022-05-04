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
/******************************************************************************
 * HEADERS
 ******************************************************************************/
#include "kvs/common_defs.h"
#include "kvs/error.h"
#include "kvs/platform_utils.h"
#include "stack_queue.h"

/******************************************************************************
 * DEFINITIONS
 ******************************************************************************/

/******************************************************************************
 * FUNCTIONS
 ******************************************************************************/
/**
 * Create a new stack/queue
 */
STATUS stack_queue_create(PStackQueue* ppStackQueue)
{
    return single_list_create(ppStackQueue);
}

/**
 * Frees and de-allocates the stack queue
 */
STATUS stack_queue_free(PStackQueue pStackQueue)
{
    return single_list_free(pStackQueue);
}

/**
 * Clears and de-allocates all the items
 */
STATUS stack_queue_clear(PStackQueue pStackQueue, BOOL freeData)
{
    return single_list_clear(pStackQueue, freeData);
}

/**
 * Gets the number of items in the stack/queue
 */
STATUS stack_queue_getCount(PStackQueue pStackQueue, PUINT32 pCount)
{
    return single_list_getNodeCount(pStackQueue, pCount);
}

/**
 * Whether the stack queue is empty
 */
STATUS stack_queue_isEmpty(PStackQueue pStackQueue, PBOOL pIsEmpty)
{
    STATUS retStatus = STATUS_SUCCESS;
    UINT32 count;

    // The call is idempotent so we shouldn't fail
    CHK(pStackQueue != NULL && pIsEmpty != NULL, STATUS_NULL_ARG);

    CHK_STATUS(single_list_getNodeCount(pStackQueue, &count));

    *pIsEmpty = (count == 0);

CleanUp:

    return retStatus;
}

/**
 * Pushes an item onto the stack
 */
STATUS stackQueuePush(PStackQueue pStackQueue, UINT64 item)
{
    return singleListInsertItemHead(pStackQueue, item);
}

/**
 * Pops an item from the stack
 */
STATUS stackQueuePop(PStackQueue pStackQueue, PUINT64 pItem)
{
    STATUS retStatus = STATUS_SUCCESS;

    CHK_STATUS(stackQueuePeek(pStackQueue, pItem));
    CHK_STATUS(single_list_deleteHead(pStackQueue));

CleanUp:

    return retStatus;
}

/**
 * Peeks an item from the stack without popping
 */
STATUS stackQueuePeek(PStackQueue pStackQueue, PUINT64 pItem)
{
    STATUS retStatus = STATUS_SUCCESS;
    PSingleListNode pHead;

    CHK_STATUS(single_list_getHeadNode(pStackQueue, &pHead));
    CHK(pHead != NULL, STATUS_NOT_FOUND);
    CHK_STATUS(single_list_getNodeData(pHead, pItem));

CleanUp:

    return retStatus;
}

/**
 * Gets the index of an item
 */
STATUS stack_queue_getIndexOf(PStackQueue pStackQueue, UINT64 item, PUINT32 pIndex)
{
    STATUS retStatus = STATUS_SUCCESS;
    UINT32 index = 0;
    UINT64 data;
    BOOL found = FALSE;
    PSingleListNode pCurNode = NULL;

    CHK(pStackQueue != NULL && pIndex != NULL, STATUS_NULL_ARG);

    CHK_STATUS(single_list_getHeadNode(pStackQueue, &pCurNode));

    while (pCurNode != NULL) {
        CHK_STATUS(single_list_getNodeData(pCurNode, &data));
        if (data == item) {
            found = TRUE;
            break;
        }

        pCurNode = pCurNode->pNext;
        index++;
    }

    CHK(found, STATUS_NOT_FOUND);

    *pIndex = index;

CleanUp:

    return retStatus;
}

/**
 * Gets an item at the given index from the stack without popping
 */
STATUS stackQueueGetAt(PStackQueue pStackQueue, UINT32 index, PUINT64 pItem)
{
    STATUS retStatus = STATUS_SUCCESS;
    CHK(pItem != NULL, STATUS_NOT_FOUND);
    CHK_STATUS(singleListGetNodeDataAt(pStackQueue, index, pItem));

CleanUp:

    return retStatus;
}

/**
 * Sets an item value at the given index from the stack without popping
 */
STATUS stackQueueSetAt(PStackQueue pStackQueue, UINT32 index, UINT64 item)
{
    STATUS retStatus = STATUS_SUCCESS;
    PSingleListNode pNode = NULL;
    CHK_STATUS(single_list_getNodeAt(pStackQueue, index, &pNode));

    // Sets the data.
    // NOTE: If the data is an allocation then it might be
    // lost so it's up to the called to ensure it's handled properly
    pNode->data = item;

CleanUp:

    return retStatus;
}

/**
 * Removes an item from the stack/queue at the given index
 */
STATUS stack_queue_removeAt(PStackQueue pStackQueue, UINT32 index)
{
    STATUS retStatus = STATUS_SUCCESS;
    PSingleListNode pNode;

    CHK_STATUS(single_list_getNodeAt(pStackQueue, index, &pNode));
    CHK_STATUS(single_list_deleteNode(pStackQueue, pNode));

CleanUp:

    return retStatus;
}

/**
 * Removes the item at the given item
 */
STATUS stack_queue_removeItem(PStackQueue pStackQueue, UINT64 item)
{
    STATUS retStatus = STATUS_SUCCESS;
    UINT32 index;

    CHK_STATUS(stack_queue_getIndexOf(pStackQueue, item, &index));
    CHK_STATUS(stack_queue_removeAt(pStackQueue, index));

CleanUp:

    return retStatus;
}

/**
 * Enqueues an item in the queue
 */
STATUS stack_queue_enqueue(PStackQueue pStackQueue, UINT64 item)
{
    return single_list_insertItemTail(pStackQueue, item);
}

STATUS stack_queue_dequeue(PStackQueue pStackQueue, PUINT64 pItem)
{
    STATUS retStatus = STATUS_SUCCESS;
    PSingleListNode pHead;

    CHK_STATUS(single_list_getHeadNode(pStackQueue, &pHead));
    CHK(pHead != NULL, STATUS_NOT_FOUND);
    CHK_STATUS(single_list_getNodeData(pHead, pItem));
    CHK_STATUS(single_list_deleteHead(pStackQueue));

CleanUp:

    return retStatus;
}

/**
 * Gets the iterator
 */
STATUS stack_queue_iterator_get(PStackQueue pStackQueue, PStackQueueIterator pIterator)
{
    STATUS retStatus = STATUS_SUCCESS;
    CHK_STATUS(single_list_getHeadNode(pStackQueue, pIterator));

CleanUp:

    return retStatus;
}

/**
 * Iterates to next
 */
STATUS stack_queue_iterator_getNext(PStackQueueIterator pIterator)
{
    STATUS retStatus = STATUS_SUCCESS;
    StackQueueIterator nextIterator;

    CHK(pIterator != NULL, STATUS_NULL_ARG);
    CHK(*pIterator != NULL, STATUS_NOT_FOUND);
    CHK_STATUS(singleListGetNextNode(*pIterator, &nextIterator));
    *pIterator = nextIterator;

CleanUp:

    return retStatus;
}

/**
 * Gets the item and the index
 */
STATUS stack_queue_iterator_getItem(StackQueueIterator iterator, PUINT64 pData)
{
    STATUS retStatus = STATUS_SUCCESS;

    CHK(iterator != NULL, STATUS_NOT_FOUND);
    CHK_STATUS(single_list_getNodeData(iterator, pData));

CleanUp:

    return retStatus;
}