#ifndef ASTERISK_LINKEDLISTS_H
#define ASTERISK_LINKEDLISTS_H

#include <pthread.h>
#include <asterisk/lock.h>

#define AST_LIST_LOCK(head)						\
	ast_mutex_lock(&head->lock) 
	
#define AST_LIST_UNLOCK(head) 						\
	ast_mutex_unlock(&head->lock)

#define AST_LIST_HEAD(name, type)					\
struct name {								\
	struct type *first;						\
	ast_mutex_t lock;						\
}

#define AST_LIST_HEAD_SET(head,entry) do {				\
	(head)->first=(entry);						\
	ast_pthread_mutex_init(&(head)->lock,NULL);				\
} while (0)

#define AST_LIST_ENTRY(type)						\
struct {								\
	struct type *next;						\
}
 
#define	AST_LIST_FIRST(head)	((head)->first)

#define AST_LIST_NEXT(elm, field)	((elm)->field.next)

#define	AST_LIST_EMPTY(head)	(AST_LIST_FIRST(head) == NULL)

#define AST_LIST_TRAVERSE(head,var,field) 				\
	for((var) = (head)->first; (var); (var) = (var)->field.next)

#define AST_LIST_HEAD_INIT(head) {						\
	(head)->first = NULL;						\
	ast_pthread_mutex_init(&(head)->lock,NULL);				\
}

#define AST_LIST_INSERT_AFTER(listelm, elm, field) do {		\
	(elm)->field.next = (listelm)->field.next;			\
	(listelm)->field.next = (elm);				\
} while (0)

#define AST_LIST_INSERT_HEAD(head, elm, field) do {			\
		(elm)->field.next = (head)->first;			\
		(head)->first = (elm);					\
} while (0)

#define AST_LIST_INSERT_TAIL(head, elm, type, field) do {             \
      struct type *curelm = (head)->first;                            \
      if(!curelm) {                                                   \
              AST_LIST_INSERT_HEAD(head, elm, field);                 \
      } else {                                                        \
              while ( curelm->field.next!=NULL ) {                    \
                      curelm=curelm->field.next;                      \
              }                                                       \
              AST_LIST_INSERT_AFTER(curelm,elm,field);                \
      }                                                               \
} while (0)


#define AST_LIST_REMOVE_HEAD(head, field) do {					\
		(head)->first = (head)->first->field.next;			\
	} while (0)

#define AST_LIST_REMOVE(head, elm, type, field) do {			\
	if ((head)->first == (elm)) {					\
		AST_LIST_REMOVE_HEAD((head), field);			\
	}								\
	else {								\
		struct type *curelm = (head)->first;			\
		while( curelm->field.next != (elm) )			\
			curelm = curelm->field.next;			\
		curelm->field.next =					\
		    curelm->field.next->field.next;			\
	}								\
} while (0)

#endif
