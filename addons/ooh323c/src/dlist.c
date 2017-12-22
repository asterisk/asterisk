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

#include "asterisk.h"
#include "asterisk/lock.h"

#include "ooasn1.h"

void dListInit (DList* pList)
{
   if (pList) {
      pList->count = 0;
      pList->head = (DListNode*) 0;
      pList->tail = (DListNode*) 0;
   }
}

DListNode* dListAppend (OOCTXT* pctxt, DList* pList, void* pData)
{
   DListNode* pListNode = (DListNode*)
      memAlloc (pctxt, sizeof(DListNode));

   if (0 != pListNode) {
      pListNode->data = pData;
      pListNode->next = (DListNode*) 0;
      if (0 != pList->tail) {
         pList->tail->next = pListNode;
         pListNode->prev = pList->tail;
      }
      if (0 == pList->head) {
         pList->head = pListNode;
         pListNode->prev = (DListNode*) 0;
      }
      pList->tail = pListNode;
      pList->count++;
   }

   return pListNode;
}

DListNode* dListAppendNode (OOCTXT* pctxt, DList* pList, void* pData)
{
   DListNode* pListNode =
      (DListNode*) (((char*)pData) - sizeof(DListNode));

   if (0 != pListNode) {
      pListNode->data = pData;
      pListNode->next = (DListNode*) 0;
      if (0 != pList->tail) {
         pList->tail->next = pListNode;
         pListNode->prev = pList->tail;
      }
      if (0 == pList->head) {
         pList->head = pListNode;
         pListNode->prev = (DListNode*) 0;
      }
      pList->tail = pListNode;
      pList->count++;
   }

   return pListNode;
}

/* Delete the head node from the list and return the data item stored	*/
/* in that node..							*/

void* dListDeleteHead (OOCTXT* pctxt, DList* pList)
{
   DListNode* pNode = (0 != pList) ? pList->head : 0;
   if (0 != pNode) {
      void* pdata = pNode->data;
      dListRemove (pList, pNode);
      memFreePtr (pctxt, pNode);
      return pdata;
   }
   return 0;
}

/* Free all nodes, but not the data */
void dListFreeNodes (OOCTXT* pctxt, DList* pList)
{
   DListNode* pNode, *pNextNode;

   for (pNode = pList->head; pNode != 0; pNode = pNextNode) {
      pNextNode = pNode->next;
      memFreePtr (pctxt, pNode);
   }
   pList->count = 0;
   pList->head = pList->tail = 0;
}

/* Free all nodes and their data */
void dListFreeAll (OOCTXT* pctxt, DList* pList)
{
   DListNode* pNode, *pNextNode;

   for (pNode = pList->head; pNode != 0; pNode = pNextNode) {
      pNextNode = pNode->next;

      memFreePtr (pctxt, pNode->data);
      memFreePtr (pctxt, pNode);
   }
   pList->count = 0;
   pList->head = pList->tail = 0;
}

/* Remove node from list. Node is not freed */
void dListRemove (DList* pList, DListNode* node)
{
   if(node->next != 0) {
      node->next->prev = node->prev;
   }
   else { /* tail */
      pList->tail = node->prev;
   }
   if(node->prev != 0) {
      node->prev->next = node->next;
   }
   else { /* head */
      pList->head = node->next;
   }
   pList->count--;
}

void dListFindAndRemove(struct OOCTXT* pctxt, DList* pList, void *data)
{
   DListNode *pNode, *pNextNode;
   for(pNode = pList->head; pNode !=0; pNode = pNextNode){
      pNextNode = pNode->next;
      if(pNode->data == data) /* pointer comparison*/
         break;
   }
   if(pNode) {
      dListRemove(pList, pNode);
      memFreePtr(pctxt, pNode);
   }
}

DListNode* dListFindByIndex (DList* pList, int index)
{
   DListNode* curNode;
   int i;

   if(index >= (int)pList->count) return 0;
   for(i = 0, curNode = pList->head; i < index && curNode != 0; i++) {
      curNode = curNode->next;
   }
   return curNode;
}

/* Insert item before given node */

DListNode* dListInsertBefore
(OOCTXT* pctxt, DList* pList, DListNode* node, const void* pData)
{
   DListNode* pListNode = (DListNode*) memAlloc (pctxt, sizeof(DListNode));

   if (0 != pListNode) {
      pListNode->data = (void*)pData;

      if (node == 0) { /* insert before end (as last element) */
         pListNode->next = (DListNode*) 0;
         if (0 != pList->tail) {
            pList->tail->next = pListNode;
            pListNode->prev = pList->tail;
         }
         if (0 == pList->head) {
            pList->head = pListNode;
            pListNode->prev = (DListNode*) 0;
         }
         pList->tail = pListNode;
      }
      else if (node == pList->head) { /* insert as head (head case) */
         pListNode->next = pList->head;
         pListNode->prev = (DListNode*) 0;
         if(pList->head != 0) {
            pList->head->prev = pListNode;
         }
         if(pList->tail == 0) {
            pList->tail = pListNode;
         }
         pList->head = pListNode;
      }
      else { /* other cases */
         pListNode->next = node;
         pListNode->prev = node->prev;
         node->prev = pListNode;
         /* here, pListNode->prev always should be non-zero,
          * because if pListNode->prev is zero - it is head case (see above).
          */
         pListNode->prev->next = pListNode;
      }

      pList->count++;
   }

   return pListNode;
}

/* Insert item after given node */

DListNode* dListInsertAfter
(OOCTXT* pctxt, DList* pList, DListNode* node, const void* pData)
{
   DListNode* pListNode = (DListNode*) memAlloc (pctxt, sizeof(DListNode));

   if (0 != pListNode) {
      pListNode->data = (void*)pData;

      if (node == 0) { /* insert as head (as first element) */
         pListNode->next = pList->head;
         pListNode->prev = (DListNode*) 0;
         if (pList->head != 0) {
            pList->head->prev = pListNode;
         }
         if (pList->tail == 0) {
            pList->tail = pListNode;
         }
         pList->head = pListNode;
      }
      else if (node == pList->tail) { /* insert as tail (as last element) */
         pListNode->next = (DListNode*) 0;
         if (0 != pList->tail) {
            pList->tail->next = pListNode;
            pListNode->prev = pList->tail;
         }
         if (0 == pList->head) {
            pList->head = pListNode;
            pListNode->prev = (DListNode*) 0;
         }
         pList->tail = pListNode;
      }
      else { /* other cases */
         pListNode->next = node->next;
         pListNode->prev = node;
         node->next = pListNode;
         /* here, pListNode->next always should be non-zero,
          * because if pListNode->next is zero - it is tail case (see above).
          */
         pListNode->next->prev = pListNode;
      }

      pList->count++;
   }

   return pListNode;
}
