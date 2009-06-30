/*
 * Copyright (C) 1997-2005 by Objective Systems, Inc.
 *
 * This software is furnished under an open source license and may be 
 * used and copied only in accordance with the terms of this license. 
 * The text of the license may generally be found in the root 
 * directory of this installation in the COPYING file.  It 
 * can also be viewed online at the following URL:
 *
 *   http://www.obj-sys.com/open/license.html
 *
 * Any redistributions of this file including modified versions must 
 * maintain this copyright notice.
 *
 *****************************************************************************/
/** 
 * @file dlist.h 
 * Doubly-linked list structures and utility functions.
 */
#ifndef _OODLIST_H_
#define _OODLIST_H_

struct OOCTXT;

/**
 * @defgroup llfuns Doubly-linked list structures and utility functions.
 * @{
 */
typedef struct _DListNode {
   void* data;
   struct _DListNode* next;
   struct _DListNode* prev;
} DListNode;

typedef struct _DList {
   unsigned int count;
   DListNode* head;
   DListNode* tail;
} DList;

#define ALLOC_ASN1ELEMDNODE(pctxt,type) \
(type*) (((char*)memHeapAllocZ (&(pctxt)->pTypeMemHeap, sizeof(type) + \
sizeof(DListNode))) + sizeof(DListNode))

#ifdef __cplusplus
extern "C" {
#endif

#ifndef EXTERN
#if defined (MAKE_DLL)
#define EXTERN __declspec(dllexport)
#elif defined (USEASN1DLL)
#define EXTERN __declspec(dllimport)
#else
#define EXTERN
#endif /* MAKE_DLL */
#endif /* EXTERN */

/**
 * This function appends an item to the linked list structure. The data item is
 * passed into the function as a void pointer that can point to any object of
 * any type. The memAlloc function is used to allocated the memory for the
 * list node structure; therefore, all internal list memory will be released
 * whenever memFree is called. The pointer to the data item itself is stored
 * in the node structure - a copy is not made.
 *
 * @param pctxt        A pointer to a context structure. This provides a
 *                     storage area for the function to store all working
 *                     variables that must be maintained between function
 *                     calls.
 * @param pList        A pointer to a linked list structure onto which the data
 *                     item is to be appended. A pointer to an updated linked
 *                     list structure.
 * @param pData        A pointer to a data item to be appended to the list.
 * @return             A pointer to an allocated node structure used to link
 *                     the given data value into the list.
 */ 
EXTERN DListNode* dListAppend 
(struct OOCTXT* pctxt, DList* pList, void* pData);

EXTERN DListNode* dListAppendNode 
(struct OOCTXT* pctxt, DList* pList, void* pData);

/**
 * This function delete the head item from the list and returns a pointer 
 * the data item stored in that node.  The memory for the node structure 
 * is released.
 *
 * @param pctxt        A pointer to a context structure. This provides a
 *                     storage area for the function to store all working
 *                     variables that must be maintained between function
 *                     calls.
 * @param pList        A pointer to the linked list structure from which 
 *                     the node will be deleted.
 * @return             A pointer to the data item stored in the deleted node.
 */ 
EXTERN void* dListDeleteHead (struct OOCTXT* pctxt, DList* pList);

EXTERN DListNode* dListFindByIndex (DList* pList, int index);

/**
 * This function initializes a doubly linked list structure. It sets the number
 * of elements to zero and sets all internal pointer values to NULL. A doubly
 * linked-list structure is described by the DList type. Nodes of the list 
 * are of type DListNode.
 *
 * Memory for the structures is allocated using the memAlloc run-time
 * function and is maintained within the context structure that is a required
 * parameter to all dList functions. This memory is released when memFree
 * is called or the Context is released. Unless otherwise noted, all data
 * passed into the list functions is simply stored on the list by value (i.e. a
 * deep-copy of the data is not done).
 *
 * @param pList        A pointer to a linked list structure to be initialized.
 */
EXTERN void dListInit (DList* pList);

/**
 * This function removes all nodes from the linked list and releases the memory
 * that was allocated for storing the node structures (DListNode). The data
 * will not be released.
 *
 * @param pctxt        A pointer to a context structure. This provides a
 *                     storage area for the function to store all working
 *                     variables that must be maintained between function
 *                     calls.
 * @param pList        A pointer to a linked list structure onto which the data
 *                     item is to be appended. A pointer to an updated linked
 *                     list structure.
 */
EXTERN void dListFreeNodes (struct OOCTXT* pctxt, DList* pList);

/** 
 * This function removes all nodes from the linked list structure and releases
 * the memory that was allocated for storing the node structures
 * (DListNode) and for data. The memory for data in each node must have
 * been previously allocated with calls to memAlloc, memAllocZ, or
 * memRealloc functions.
 *
 * @param pctxt        Pointer to a context structure. This provides a
 *                     storage area for the function to store all working
 *                     variables that must be maintained between function
 *                     calls.
 * @param pList        Pointer to a linked list structure.
 */
EXTERN void dListFreeAll (struct OOCTXT* pctxt, DList* pList);

/**
 * This function inserts an item into the linked list structure before the 
 * specified element.
 * 
 * @param pctxt         Pointer to a context structure.
 * @param pList		A pointer to a linked list structure into which the 
 *                        data item is to be inserted.
 * @param node          The position in the list where the item is to be 
 *                        inserted.  The item will be inserted before this 
 *                        node or appended to the list if node is null.
 * @param pData		A pointer to the data item to be inserted to the list.
 * @return		A pointer to an allocated node structure used to 
 *                        link the given data value into the list.
 */
EXTERN DListNode* dListInsertBefore 
(struct OOCTXT* pctxt, DList* pList, DListNode* node, const void* pData);

/**
 * This function inserts an item into the linked list structure after the 
 * specified element.
 * 
 * @param pctxt         Pointer to a context structure.
 * @param pList		A pointer to a linked list structure into which the 
 *                        data item is to be inserted.
 * @param node          The position in the list where the item is to be 
 *                        inserted.  The item will be inserted after this 
 *                        node or added as the head element if node is null.
 * @param pData		A pointer to the data item to be inserted to the list.
 * @return		A pointer to an allocated node structure used to 
 *                        link the given data value into the list.
 */
EXTERN DListNode* dListInsertAfter 
(struct OOCTXT* pctxt, DList* pList, DListNode* node, const void* pData);

/**
 * This function removes a node from the linked list structure. The memAlloc
 * function was used to allocate the memory for the list node structure,
 * therefore, all internal list memory will be released whenever memFree or
 * memFreePtr is called.
 *
 * @param pList        A pointer to a linked list structure onto which the data
 *                     item is to be removed. A pointer to an updated linked
 *                     list structure.
 * @param node         A pointer to the node that is to be removed. It should
 *                     already be in the linked list structure.
 */
EXTERN void  dListRemove (DList* pList, DListNode* node);
void dListFindAndRemove(DList* pList, void* data);

/** 
 * @}
 */
#ifdef __cplusplus
}
#endif

#endif
