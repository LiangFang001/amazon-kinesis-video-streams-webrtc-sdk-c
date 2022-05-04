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
#ifndef __AWS_KVS_WEBRTC_SINGLE_LINKED_LIST_INCLUDE__
#define __AWS_KVS_WEBRTC_SINGLE_LINKED_LIST_INCLUDE__

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************
 * HEADERS
 ******************************************************************************/
/******************************************************************************
 * DEFINITIONS
 ******************************************************************************/
typedef struct __SingleListNode {
    struct __SingleListNode* pNext;
    UINT64 data;
} SingleListNode, *PSingleListNode;

typedef struct {
    UINT32 count;
    PSingleListNode pHead;
    PSingleListNode pTail;
} SingleList, *PSingleList;

/******************************************************************************
 * FUNCTIONS
 ******************************************************************************/
/**
 * Create a new single linked list
 */
STATUS single_list_create(PSingleList*);
/**
 * @brief Frees a single linked list and deallocates the nodes
 *
 * @param[in] pList the context of the single list.
 *
 * @return STATUS code of the execution. STATUS_SUCCESS on success.
 */
STATUS single_list_free(PSingleList pList);
/**
 * @brief Clears a single linked list and deallocates all the items.
 *
 * @param[in] pList the context of the single list.
 * @param[in] freeData
 *
 * @return STATUS code of the execution. STATUS_SUCCESS on success.
 */
STATUS single_list_clear(PSingleList pList, BOOL freeData);

/**
 * Insert a node in the head position in the list
 */
STATUS singleListInsertNodeHead(PSingleList, PSingleListNode);

/**
 * Insert a new node with the data at the head position in the list
 */
STATUS singleListInsertItemHead(PSingleList, UINT64);

/**
 * Insert a node in the tail position in the list
 */
STATUS singleListInsertNodeTail(PSingleList, PSingleListNode);

/**
 * Insert a new node with the data at the tail position in the list
 */
STATUS single_list_insertItemTail(PSingleList, UINT64);

/**
 * Insert a node after a given node
 */
STATUS singleListInsertNodeAfter(PSingleList, PSingleListNode, PSingleListNode);

/**
 * Insert a new node with the data after a given node
 */
STATUS singleListInsertItemAfter(PSingleList, PSingleListNode, UINT64);
/**
 * @brief Removes and deletes the head, but does not free the data inside the head node.
 *
 * @param[in] pList the context of the single list.
 *
 * @return STATUS code of the execution. STATUS_SUCCESS on success.
 */
STATUS single_list_deleteHead(PSingleList pList);

/**
 * Removes and deletes the specified node
 */
STATUS single_list_deleteNode(PSingleList, PSingleListNode);

/**
 * Removes and deletes the next node of the specified node
 */
STATUS single_list_deleteNextNode(PSingleList, PSingleListNode);
/**
 * @brief Gets the head node.
 *
 * @param[in] pList the context of the single list.
 * @param[in, out] ppNode
 *
 * @return STATUS code of the execution. STATUS_SUCCESS on success.
 */
STATUS single_list_getHeadNode(PSingleList pList, PSingleListNode* ppNode);
/**
 * Gets the tail node
 */
STATUS singleListGetTailNode(PSingleList, PSingleListNode*);

/**
 * Gets the node at the specified index
 */
STATUS single_list_getNodeAt(PSingleList, UINT32, PSingleListNode*);

/**
 * Gets the node data at the specified index
 */
STATUS singleListGetNodeDataAt(PSingleList, UINT32, PUINT64);

/**
 * Gets the node data
 */
STATUS single_list_getNodeData(PSingleListNode, PUINT64);

/**
 * Gets the next node
 */
STATUS singleListGetNextNode(PSingleListNode, PSingleListNode*);
/**
 * @brief Gets the count of nodes in the list
 *
 * @param[in] pList the context of the single list.
 * @param[in] pCount
 *
 * @return STATUS code of the execution. STATUS_SUCCESS on success.
 */
STATUS single_list_getNodeCount(PSingleList pList, PUINT32 pCount);

/**
 * Append a single list to the other and then free the list being appended
 */
STATUS singleListAppendList(PSingleList, PSingleList*);

#ifdef __cplusplus
}
#endif
#endif /* __AWS_KVS_WEBRTC_SINGLE_LINKED_LIST_INCLUDE__ */
