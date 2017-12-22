/*
 * astobj2_hash - RBTree implementation for astobj2.
 *
 * Copyright (C) 2006 Marta Carbone, Luigi Rizzo - Univ. di Pisa, Italy
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 *
 * \brief RBTree functions implementing astobj2 containers.
 *
 * \author Richard Mudgett <rmudgett@digium.com>
 */

#include "asterisk.h"

#include "asterisk/_private.h"
#include "asterisk/astobj2.h"
#include "asterisk/utils.h"
#include "astobj2_private.h"
#include "astobj2_container_private.h"

/*!
 * A structure to hold the object held by the container and
 * where it is located in it.
 *
 * A red-black tree has the following properties:
 *
 * 1) Every node is either black or red.
 *
 * 2) The root is black.
 *
 * 3) If a node has a NULL child, that "child" is considered
 * black.
 *
 * 4) If a node is red, then both of its children are black.
 *
 * 5) Every path from a node to a descendant NULL child has the
 * same number of black nodes.  (Including the black NULL
 * child.)
 */
struct rbtree_node {
	/*!
	 * \brief Items common to all container nodes.
	 * \note Must be first in the specific node struct.
	 */
	struct ao2_container_node common;
	/*! Parent node of this node. NULL if this is the root node. */
	struct rbtree_node *parent;
	/*! Left child node of this node.  NULL if does not have this child. */
	struct rbtree_node *left;
	/*! Right child node of this node.  NULL if does not have this child. */
	struct rbtree_node *right;
	/*! TRUE if the node is red. */
	unsigned int is_red:1;
};

/*!
 * A rbtree container in addition to values common to all
 * container types, stores the pointer to the root node of the
 * tree.
 */
struct ao2_container_rbtree {
	/*!
	 * \brief Items common to all containers.
	 * \note Must be first in the specific container struct.
	 */
	struct ao2_container common;
	/*! Root node of the tree.  NULL if the tree is empty. */
	struct rbtree_node *root;
#if defined(AO2_DEBUG)
	struct {
		/*! Fixup insert left cases 1-3 */
		int fixup_insert_left[3];
		/*! Fixup insert right cases 1-3 */
		int fixup_insert_right[3];
		/*! Fixup delete left cases 1-4 */
		int fixup_delete_left[4];
		/*! Fixup delete right cases 1-4 */
		int fixup_delete_right[4];
		/*! Deletion of node with number of children (0-2). */
		int delete_children[3];
	} stats;
#endif	/* defined(AO2_DEBUG) */
};

enum equal_node_bias {
	/*! Bias search toward first matching node in the container. */
	BIAS_FIRST,
	/*! Bias search toward any matching node. */
	BIAS_EQUAL,
	/*! Bias search toward last matching node in the container. */
	BIAS_LAST,
};

enum empty_node_direction {
	GO_LEFT,
	GO_RIGHT,
};

/*! Traversal state to restart a rbtree container traversal. */
struct rbtree_traversal_state {
	/*! Active sort function in the traversal if not NULL. */
	ao2_sort_fn *sort_fn;
	/*! Saved comparison callback arg pointer. */
	void *arg;
	/*! Saved search flags to control traversing the container. */
	enum search_flags flags;
};

struct rbtree_traversal_state_check {
	/*
	 * If we have a division by zero compile error here then there
	 * is not enough room for the state.  Increase AO2_TRAVERSAL_STATE_SIZE.
	 */
	char check[1 / (AO2_TRAVERSAL_STATE_SIZE / sizeof(struct rbtree_traversal_state))];
};

/*!
 * \internal
 * \brief Get the most left node in the tree.
 * \since 12.0.0
 *
 * \param node Starting node to find the most left node.
 *
 * \return Left most node.  Never NULL.
 */
static struct rbtree_node *rb_node_most_left(struct rbtree_node *node)
{
	while (node->left) {
		node = node->left;
	}

	return node;
}

/*!
 * \internal
 * \brief Get the most right node in the tree.
 * \since 12.0.0
 *
 * \param node Starting node to find the most right node.
 *
 * \return Right most node.  Never NULL.
 */
static struct rbtree_node *rb_node_most_right(struct rbtree_node *node)
{
	while (node->right) {
		node = node->right;
	}

	return node;
}

/*!
 * \internal
 * \brief Get the next node in ascending sequence.
 * \since 12.0.0
 *
 * \param node Starting node to find the next node.
 *
 * \retval node on success.
 * \retval NULL if no node.
 */
static struct rbtree_node *rb_node_next(struct rbtree_node *node)
{
	if (node->right) {
		return rb_node_most_left(node->right);
	}

	/* Find the parent that the node is a left child of. */
	while (node->parent) {
		if (node->parent->left == node) {
			/* We are the left child.  The parent is the next node. */
			return node->parent;
		}
		node = node->parent;
	}
	return NULL;
}

/*!
 * \internal
 * \brief Get the next node in descending sequence.
 * \since 12.0.0
 *
 * \param node Starting node to find the previous node.
 *
 * \retval node on success.
 * \retval NULL if no node.
 */
static struct rbtree_node *rb_node_prev(struct rbtree_node *node)
{
	if (node->left) {
		return rb_node_most_right(node->left);
	}

	/* Find the parent that the node is a right child of. */
	while (node->parent) {
		if (node->parent->right == node) {
			/* We are the right child.  The parent is the previous node. */
			return node->parent;
		}
		node = node->parent;
	}
	return NULL;
}

/*!
 * \internal
 * \brief Get the next node in pre-order sequence.
 * \since 12.0.0
 *
 * \param node Starting node to find the next node.
 *
 * \retval node on success.
 * \retval NULL if no node.
 */
static struct rbtree_node *rb_node_pre(struct rbtree_node *node)
{
	/* Visit the children if the node has any. */
	if (node->left) {
		return node->left;
	}
	if (node->right) {
		return node->right;
	}

	/* Time to go back up. */
	for (;;) {
		if (!node->parent) {
			return NULL;
		}
		if (node->parent->left == node && node->parent->right) {
			/*
			 * We came up the left child and there's a right child.  Visit
			 * it.
			 */
			return node->parent->right;
		}
		node = node->parent;
	}
}

/*!
 * \internal
 * \brief Get the next node in post-order sequence.
 * \since 12.0.0
 *
 * \param node Starting node to find the next node.
 *
 * \retval node on success.
 * \retval NULL if no node.
 */
static struct rbtree_node *rb_node_post(struct rbtree_node *node)
{
	/* This node's children have already been visited. */
	for (;;) {
		if (!node->parent) {
			return NULL;
		}
		if (node->parent->left == node) {
			/* We came up the left child. */
			node = node->parent;

			/*
			 * Find the right child's left most childless node.
			 */
			while (node->right) {
				node = rb_node_most_left(node->right);
			}

			/*
			 * This node's left child has already been visited or it doesn't
			 * have any children.
			 */
			return node;
		}

		/*
		 * We came up the right child.
		 *
		 * This node's children have already been visited.  Time to
		 * visit the parent.
		 */
		return node->parent;
	}
}

/*!
 * \internal
 * \brief Get the next non-empty node in ascending sequence.
 * \since 12.0.0
 *
 * \param node Starting node to find the next node.
 *
 * \retval node on success.
 * \retval NULL if no node.
 */
static struct rbtree_node *rb_node_next_full(struct rbtree_node *node)
{
	for (;;) {
		node = rb_node_next(node);
		if (!node || node->common.obj) {
			return node;
		}
	}
}

/*!
 * \internal
 * \brief Get the next non-empty node in descending sequence.
 * \since 12.0.0
 *
 * \param node Starting node to find the previous node.
 *
 * \retval node on success.
 * \retval NULL if no node.
 */
static struct rbtree_node *rb_node_prev_full(struct rbtree_node *node)
{
	for (;;) {
		node = rb_node_prev(node);
		if (!node || node->common.obj) {
			return node;
		}
	}
}

/*!
 * \internal
 * \brief Determine which way to go from an empty node.
 * \since 12.0.0
 *
 * \param empty Empty node to determine which side obj_right goes on.
 * \param sort_fn Sort comparison function for non-empty nodes.
 * \param obj_right pointer to the (user-defined part) of an object.
 * \param flags flags from ao2_callback()
 *   OBJ_SEARCH_OBJECT - if set, 'obj_right', is an object.
 *   OBJ_SEARCH_KEY - if set, 'obj_right', is a search key item that is not an object.
 *   OBJ_SEARCH_PARTIAL_KEY - if set, 'obj_right', is a partial search key item that is not an object.
 * \param bias How to bias search direction for duplicates
 *
 * \return enum empty_node_direction to proceed.
 */
static enum empty_node_direction rb_find_empty_direction(struct rbtree_node *empty, ao2_sort_fn *sort_fn, void *obj_right, enum search_flags flags, enum equal_node_bias bias)
{
	int cmp;
	struct rbtree_node *cur;
	struct rbtree_node *right_most;

	/* Try for a quick definite go left. */
	if (!empty->left) {
		/* The empty node has no left child. */
		return GO_RIGHT;
	}
	right_most = rb_node_most_right(empty->left);
	if (right_most->common.obj) {
		cmp = sort_fn(right_most->common.obj, obj_right, flags);
		if (cmp < 0) {
			return GO_RIGHT;
		}
		if (cmp == 0 && bias == BIAS_LAST) {
			return GO_RIGHT;
		}
		return GO_LEFT;
	}

	/* Try for a quick definite go right. */
	if (!empty->right) {
		/* The empty node has no right child. */
		return GO_LEFT;
	}
	cur = rb_node_most_left(empty->right);
	if (cur->common.obj) {
		cmp = sort_fn(cur->common.obj, obj_right, flags);
		if (cmp > 0) {
			return GO_LEFT;
		}
		if (cmp == 0 && bias == BIAS_FIRST) {
			return GO_LEFT;
		}
		return GO_RIGHT;
	}

	/*
	 * Have to scan the previous nodes from the right_most node of
	 * the left subtree for the first non-empty node to determine
	 * direction.
	 */
	cur = right_most;
	for (;;) {
		/* Find previous node. */
		if (cur->left) {
			cur = rb_node_most_right(cur->left);
		} else {
			/* Find the parent that the node is a right child of. */
			for (;;) {
				if (cur->parent == empty) {
					/* The left side of the empty node is all empty nodes. */
					return GO_RIGHT;
				}
				if (cur->parent->right == cur) {
					/* We are the right child.  The parent is the previous node. */
					cur = cur->parent;
					break;
				}
				cur = cur->parent;
			}
		}

		if (cur->common.obj) {
			cmp = sort_fn(cur->common.obj, obj_right, flags);
			if (cmp < 0) {
				return GO_RIGHT;
			}
			if (cmp == 0 && bias == BIAS_LAST) {
				return GO_RIGHT;
			}
			return GO_LEFT;
		}
	}
}

/*!
 * \internal
 * \brief Tree node rotation left.
 * \since 12.0.0
 *
 * \param self Container holding node.
 * \param node Node to perform a left rotation with.
 *
 *        p                         p
 *        |     Left rotation       |
 *        N        --->             Ch
 *       / \                       / \
 *      a  Ch                     N   c
 *        / \                    / \
 *       b   c                  a   b
 *
 * N = node
 * Ch = child
 * p = parent
 * a,b,c = other nodes that are unaffected by the rotation.
 *
 * \note It is assumed that the node's right child exists.
 *
 * \return Nothing
 */
static void rb_rotate_left(struct ao2_container_rbtree *self, struct rbtree_node *node)
{
	struct rbtree_node *child;	/*!< Node's right child. */

	child = node->right;

	/* Link the node's parent to the child. */
	if (!node->parent) {
		/* Node is the root so we get a new root node. */
		self->root = child;
	} else if (node->parent->left == node) {
		/* Node is a left child. */
		node->parent->left = child;
	} else {
		/* Node is a right child. */
		node->parent->right = child;
	}
	child->parent = node->parent;

	/* Link node's right subtree to the child's left subtree. */
	node->right = child->left;
	if (node->right) {
		node->right->parent = node;
	}

	/* Link the node to the child's left. */
	node->parent = child;
	child->left = node;
}

/*!
 * \internal
 * \brief Tree node rotation right.
 * \since 12.0.0
 *
 * \param self Container holding node.
 * \param node Node to perform a right rotation with.
 *
 *        p                         p
 *        |     Right rotation      |
 *        Ch                        N
 *       / \       <---            / \
 *      a  N                      Ch  c
 *        / \                    / \
 *       b   c                  a   b
 *
 * N = node
 * Ch = child
 * p = parent
 * a,b,c = other nodes that are unaffected by the rotation.
 *
 * \note It is assumed that the node's left child exists.
 *
 * \return Nothing
 */
static void rb_rotate_right(struct ao2_container_rbtree *self, struct rbtree_node *node)
{
	struct rbtree_node *child;	/*!< Node's left child. */

	child = node->left;

	/* Link the node's parent to the child. */
	if (!node->parent) {
		/* Node is the root so we get a new root node. */
		self->root = child;
	} else if (node->parent->right == node) {
		/* Node is a right child. */
		node->parent->right = child;
	} else {
		/* Node is a left child. */
		node->parent->left = child;
	}
	child->parent = node->parent;

	/* Link node's left subtree to the child's right subtree. */
	node->left = child->right;
	if (node->left) {
		node->left->parent = node;
	}

	/* Link the node to the child's right. */
	node->parent = child;
	child->right = node;
}

/*!
 * \internal
 * \brief Create an empty copy of this container. (Debug version)
 * \since 14.0.0
 *
 * \param self Container to operate upon.
 * \param tag used for debugging.
 * \param file Debug file name invoked from
 * \param line Debug line invoked from
 * \param func Debug function name invoked from
 *
 * \retval empty-clone-container on success.
 * \retval NULL on error.
 */
static struct ao2_container *rb_ao2_alloc_empty_clone(struct ao2_container_rbtree *self,
	const char *tag, const char *file, int line, const char *func)
{
	if (!__is_ao2_object(self, file, line, func)) {
		return NULL;
	}

	return __ao2_container_alloc_rbtree(ao2_options_get(self), self->common.options,
		self->common.sort_fn, self->common.cmp_fn, tag, file, line, func);
}

/*!
 * \internal
 * \brief Fixup the rbtree after deleting a node.
 * \since 12.0.0
 *
 * \param self Container to operate upon.
 * \param child Child of the node just deleted from the container.
 *
 * \note The child must be a dummy black node if there really
 * was no child of the deleted node.  Otherwise, the caller must
 * pass in the parent node and which child was deleted.  In
 * addition, the fixup routine would be more complicated.
 *
 * \return Nothing
 */
static void rb_delete_fixup(struct ao2_container_rbtree *self, struct rbtree_node *child)
{
	struct rbtree_node *sibling;

	while (self->root != child && !child->is_red) {
		if (child->parent->left == child) {
			/* Child is a left child. */
			sibling = child->parent->right;
			ast_assert(sibling != NULL);
			if (sibling->is_red) {
				/* Case 1: The child's sibling is red. */
				AO2_DEVMODE_STAT(++self->stats.fixup_delete_left[0]);
				sibling->is_red = 0;
				child->parent->is_red = 1;
				rb_rotate_left(self, child->parent);
				sibling = child->parent->right;
				ast_assert(sibling != NULL);
			}
			/*
			 * The sibling is black.  A black node must have two children,
			 * or one red child, or no children.
			 */
			if ((!sibling->left || !sibling->left->is_red)
				&& (!sibling->right || !sibling->right->is_red)) {
				/*
				 * Case 2: The sibling is black and both of its children are black.
				 *
				 * This case handles the two black children or no children
				 * possibilities of a black node.
				 */
				AO2_DEVMODE_STAT(++self->stats.fixup_delete_left[1]);
				sibling->is_red = 1;
				child = child->parent;
			} else {
				/* At this point the sibling has at least one red child. */
				if (!sibling->right || !sibling->right->is_red) {
					/*
					 * Case 3: The sibling is black, its left child is red, and its
					 * right child is black.
					 */
					AO2_DEVMODE_STAT(++self->stats.fixup_delete_left[2]);
					ast_assert(sibling->left != NULL);
					ast_assert(sibling->left->is_red);
					sibling->left->is_red = 0;
					sibling->is_red = 1;
					rb_rotate_right(self, sibling);
					sibling = child->parent->right;
					ast_assert(sibling != NULL);
				}
				/* Case 4: The sibling is black and its right child is red. */
				AO2_DEVMODE_STAT(++self->stats.fixup_delete_left[3]);
				sibling->is_red = child->parent->is_red;
				child->parent->is_red = 0;
				if (sibling->right) {
					sibling->right->is_red = 0;
				}
				rb_rotate_left(self, child->parent);
				child = self->root;
			}
		} else {
			/* Child is a right child. */
			sibling = child->parent->left;
			ast_assert(sibling != NULL);
			if (sibling->is_red) {
				/* Case 1: The child's sibling is red. */
				AO2_DEVMODE_STAT(++self->stats.fixup_delete_right[0]);
				sibling->is_red = 0;
				child->parent->is_red = 1;
				rb_rotate_right(self, child->parent);
				sibling = child->parent->left;
				ast_assert(sibling != NULL);
			}
			/*
			 * The sibling is black.  A black node must have two children,
			 * or one red child, or no children.
			 */
			if ((!sibling->right || !sibling->right->is_red)
				&& (!sibling->left || !sibling->left->is_red)) {
				/*
				 * Case 2: The sibling is black and both of its children are black.
				 *
				 * This case handles the two black children or no children
				 * possibilities of a black node.
				 */
				AO2_DEVMODE_STAT(++self->stats.fixup_delete_right[1]);
				sibling->is_red = 1;
				child = child->parent;
			} else {
				/* At this point the sibling has at least one red child. */
				if (!sibling->left || !sibling->left->is_red) {
					/*
					 * Case 3: The sibling is black, its right child is red, and its
					 * left child is black.
					 */
					AO2_DEVMODE_STAT(++self->stats.fixup_delete_right[2]);
					ast_assert(sibling->right != NULL);
					ast_assert(sibling->right->is_red);
					sibling->right->is_red = 0;
					sibling->is_red = 1;
					rb_rotate_left(self, sibling);
					sibling = child->parent->left;
					ast_assert(sibling != NULL);
				}
				/* Case 4: The sibling is black and its left child is red. */
				AO2_DEVMODE_STAT(++self->stats.fixup_delete_right[3]);
				sibling->is_red = child->parent->is_red;
				child->parent->is_red = 0;
				if (sibling->left) {
					sibling->left->is_red = 0;
				}
				rb_rotate_right(self, child->parent);
				child = self->root;
			}
		}
	}

	/*
	 * Case 2 could leave the child node red and it needs to leave
	 * with it black.
	 *
	 * Case 4 sets the child node to the root which of course must
	 * be black.
	 */
	child->is_red = 0;
}

/*!
 * \internal
 * \brief Delete the doomed node from this container.
 * \since 12.0.0
 *
 * \param self Container to operate upon.
 * \param doomed Container node to delete from the container.
 *
 * \return Nothing
 */
static void rb_delete_node(struct ao2_container_rbtree *self, struct rbtree_node *doomed)
{
	struct rbtree_node *child;
	int need_fixup;

	if (doomed->left && doomed->right) {
		struct rbtree_node *next;
		int is_red;

		/*
		 * The doomed node has two children.
		 *
		 * Find the next child node and swap it with the doomed node in
		 * the tree.
		 */
		AO2_DEVMODE_STAT(++self->stats.delete_children[2]);
		next = rb_node_most_left(doomed->right);
		SWAP(doomed->parent, next->parent);
		SWAP(doomed->left, next->left);
		SWAP(doomed->right, next->right);
		is_red = doomed->is_red;
		doomed->is_red = next->is_red;
		next->is_red = is_red;

		/* Link back in the next node. */
		if (!next->parent) {
			/* Doomed was the root so we get a new root node. */
			self->root = next;
		} else if (next->parent->left == doomed) {
			/* Doomed was the left child. */
			next->parent->left = next;
		} else {
			/* Doomed was the right child. */
			next->parent->right = next;
		}
		next->left->parent = next;
		if (next->right == next) {
			/* The next node was the right child of doomed. */
			next->right = doomed;
			doomed->parent = next;
		} else {
			next->right->parent = next;
			doomed->parent->left = doomed;
		}

		/* The doomed node has no left child now. */
		ast_assert(doomed->left == NULL);

		/*
		 * We don't have to link the right child back in with doomed
		 * since we are going to link it with doomed's parent anyway.
		 */
		child = doomed->right;
	} else {
		/* Doomed has at most one child. */
		child = doomed->left;
		if (!child) {
			child = doomed->right;
		}
	}
	if (child) {
		AO2_DEVMODE_STAT(++self->stats.delete_children[1]);
	} else {
		AO2_DEVMODE_STAT(++self->stats.delete_children[0]);
	}

	need_fixup = (!doomed->is_red && !self->common.destroying);
	if (need_fixup && !child) {
		/*
		 * Use the doomed node as a place holder node for the
		 * nonexistent child so we also don't have to pass to the fixup
		 * routine the parent and which child the deleted node came
		 * from.
		 */
		rb_delete_fixup(self, doomed);
		ast_assert(doomed->left == NULL);
		ast_assert(doomed->right == NULL);
		ast_assert(!doomed->is_red);
	}

	/* Link the child in place of doomed. */
	if (!doomed->parent) {
		/* Doomed was the root so we get a new root node. */
		self->root = child;
	} else if (doomed->parent->left == doomed) {
		/* Doomed was the left child. */
		doomed->parent->left = child;
	} else {
		/* Doomed was the right child. */
		doomed->parent->right = child;
	}
	if (child) {
		child->parent = doomed->parent;
		if (need_fixup) {
			rb_delete_fixup(self, child);
		}
	}

	AO2_DEVMODE_STAT(--self->common.nodes);
}

/*!
 * \internal
 * \brief Destroy a rbtree container node.
 * \since 12.0.0
 *
 * \param v_doomed Container node to destroy.
 *
 * \details
 * The container node unlinks itself from the container as part
 * of its destruction.  The node must be destroyed while the
 * container is already locked.
 *
 * \note The container must be locked when the node is
 * unreferenced.
 *
 * \return Nothing
 */
static void rb_ao2_node_destructor(void *v_doomed)
{
	struct rbtree_node *doomed = v_doomed;

	if (doomed->common.is_linked) {
		struct ao2_container_rbtree *my_container;

		/*
		 * Promote to write lock if not already there.  Since
		 * adjust_lock() can potentially release and block waiting for a
		 * write lock, care must be taken to ensure that node references
		 * are released before releasing the container references.
		 *
		 * Node references held by an iterator can only be held while
		 * the iterator also holds a reference to the container.  These
		 * node references must be unreferenced before the container can
		 * be unreferenced to ensure that the node will not get a
		 * negative reference and the destructor called twice for the
		 * same node.
		 */
		my_container = (struct ao2_container_rbtree *) doomed->common.my_container;
#ifdef AST_DEVMODE
		is_ao2_object(my_container);
#endif

		__adjust_lock(my_container, AO2_LOCK_REQ_WRLOCK, 1);

#if defined(AO2_DEBUG)
		if (!my_container->common.destroying
			&& ao2_container_check(doomed->common.my_container, OBJ_NOLOCK)) {
			ast_log(LOG_ERROR, "Container integrity failed before node deletion.\n");
		}
#endif	/* defined(AO2_DEBUG) */
		rb_delete_node(my_container, doomed);
#if defined(AO2_DEBUG)
		if (!my_container->common.destroying
			&& ao2_container_check(doomed->common.my_container, OBJ_NOLOCK)) {
			ast_log(LOG_ERROR, "Container integrity failed after node deletion.\n");
		}
#endif	/* defined(AO2_DEBUG) */
	}

	/*
	 * We could have an object in the node if the container is being
	 * destroyed or the node had not been linked in yet.
	 */
	if (doomed->common.obj) {
		__container_unlink_node(&doomed->common, AO2_UNLINK_NODE_UNLINK_OBJECT);
	}
}

/*!
 * \internal
 * \brief Create a new container node.
 * \since 12.0.0
 *
 * \param self Container to operate upon.
 * \param obj_new Object to put into the node.
 * \param tag used for debugging.
 * \param file Debug file name invoked from
 * \param line Debug line invoked from
 * \param func Debug function name invoked from
 *
 * \retval initialized-node on success.
 * \retval NULL on error.
 */
static struct rbtree_node *rb_ao2_new_node(struct ao2_container_rbtree *self, void *obj_new, const char *tag, const char *file, int line, const char *func)
{
	struct rbtree_node *node;

	node = ao2_t_alloc_options(sizeof(*node), rb_ao2_node_destructor, AO2_ALLOC_OPT_LOCK_NOLOCK, NULL);
	if (!node) {
		return NULL;
	}

	__ao2_ref(obj_new, +1, tag ?: "Container node creation", file, line, func);
	node->common.obj = obj_new;
	node->common.my_container = (struct ao2_container *) self;

	return node;
}

/*!
 * \internal
 * \brief Fixup the rbtree after inserting a node.
 * \since 12.0.0
 *
 * \param self Container to operate upon.
 * \param node Container node just inserted into the container.
 *
 * \note The just inserted node is red.
 *
 * \return Nothing
 */
static void rb_insert_fixup(struct ao2_container_rbtree *self, struct rbtree_node *node)
{
	struct rbtree_node *g_parent;	/* Grand parent node. */

	while (node->parent && node->parent->is_red) {
		g_parent = node->parent->parent;

		/* The grand parent must exist if the parent is red. */
		ast_assert(g_parent != NULL);

		if (node->parent == g_parent->left) {
			/* The parent is a left child. */
			if (g_parent->right && g_parent->right->is_red) {
				/* Case 1: Push the black down from the grand parent node. */
				AO2_DEVMODE_STAT(++self->stats.fixup_insert_left[0]);
				g_parent->right->is_red = 0;
				g_parent->left->is_red = 0;
				g_parent->is_red = 1;

				node = g_parent;
			} else {
				/* The uncle node is black. */
				if (node->parent->right == node) {
					/*
					 * Case 2: The node is a right child.
					 *
					 * Which node is the grand parent does not change.
					 */
					AO2_DEVMODE_STAT(++self->stats.fixup_insert_left[1]);
					node = node->parent;
					rb_rotate_left(self, node);
				}
				/* Case 3: The node is a left child. */
				AO2_DEVMODE_STAT(++self->stats.fixup_insert_left[2]);
				node->parent->is_red = 0;
				g_parent->is_red = 1;
				rb_rotate_right(self, g_parent);
			}
		} else {
			/* The parent is a right child. */
			if (g_parent->left && g_parent->left->is_red) {
				/* Case 1: Push the black down from the grand parent node. */
				AO2_DEVMODE_STAT(++self->stats.fixup_insert_right[0]);
				g_parent->left->is_red = 0;
				g_parent->right->is_red = 0;
				g_parent->is_red = 1;

				node = g_parent;
			} else {
				/* The uncle node is black. */
				if (node->parent->left == node) {
					/*
					 * Case 2: The node is a left child.
					 *
					 * Which node is the grand parent does not change.
					 */
					AO2_DEVMODE_STAT(++self->stats.fixup_insert_right[1]);
					node = node->parent;
					rb_rotate_right(self, node);
				}
				/* Case 3: The node is a right child. */
				AO2_DEVMODE_STAT(++self->stats.fixup_insert_right[2]);
				node->parent->is_red = 0;
				g_parent->is_red = 1;
				rb_rotate_left(self, g_parent);
			}
		}
	}

	/*
	 * The root could be red here because:
	 * 1) We just inserted the root node in an empty tree.
	 *
	 * 2) Case 1 could leave the root red if the grand parent were
	 * the root.
	 */
	self->root->is_red = 0;
}

/*!
 * \internal
 * \brief Insert a node into this container.
 * \since 12.0.0
 *
 * \param self Container to operate upon.
 * \param node Container node to insert into the container.
 *
 * \return enum ao2_container_insert value.
 */
static enum ao2_container_insert rb_ao2_insert_node(struct ao2_container_rbtree *self, struct rbtree_node *node)
{
	int cmp;
	struct rbtree_node *cur;
	struct rbtree_node *next;
	ao2_sort_fn *sort_fn;
	uint32_t options;
	enum equal_node_bias bias;

	if (!self->root) {
		/* The tree is empty. */
		self->root = node;
		return AO2_CONTAINER_INSERT_NODE_INSERTED;
	}

	sort_fn = self->common.sort_fn;
	options = self->common.options;
	switch (options & AO2_CONTAINER_ALLOC_OPT_DUPS_MASK) {
	default:
	case AO2_CONTAINER_ALLOC_OPT_DUPS_ALLOW:
		if (options & AO2_CONTAINER_ALLOC_OPT_INSERT_BEGIN) {
			bias = BIAS_FIRST;
		} else {
			bias = BIAS_LAST;
		}
		break;
	case AO2_CONTAINER_ALLOC_OPT_DUPS_REJECT:
	case AO2_CONTAINER_ALLOC_OPT_DUPS_OBJ_REJECT:
	case AO2_CONTAINER_ALLOC_OPT_DUPS_REPLACE:
		bias = BIAS_EQUAL;
		break;
	}

	/*
	 * New nodes are always colored red when initially inserted into
	 * the tree.  (Except for the root which is always black.)
	 */
	node->is_red = 1;

	/* Find node where normal insert would put a new node. */
	cur = self->root;
	for (;;) {
		if (!cur->common.obj) {
			/* Which direction do we go to insert this node? */
			if (rb_find_empty_direction(cur, sort_fn, node->common.obj, OBJ_SEARCH_OBJECT, bias)
				== GO_LEFT) {
				if (cur->left) {
					cur = cur->left;
					continue;
				}

				/* Node becomes a left child */
				cur->left = node;
				node->parent = cur;
				rb_insert_fixup(self, node);
				return AO2_CONTAINER_INSERT_NODE_INSERTED;
			}
			if (cur->right) {
				cur = cur->right;
				continue;
			}

			/* Node becomes a right child */
			cur->right = node;
			node->parent = cur;
			rb_insert_fixup(self, node);
			return AO2_CONTAINER_INSERT_NODE_INSERTED;
		}
		cmp = sort_fn(cur->common.obj, node->common.obj, OBJ_SEARCH_OBJECT);
		if (cmp > 0) {
			if (cur->left) {
				cur = cur->left;
				continue;
			}

			/* Node becomes a left child */
			cur->left = node;
			node->parent = cur;
			rb_insert_fixup(self, node);
			return AO2_CONTAINER_INSERT_NODE_INSERTED;
		} else if (cmp < 0) {
			if (cur->right) {
				cur = cur->right;
				continue;
			}

			/* Node becomes a right child */
			cur->right = node;
			node->parent = cur;
			rb_insert_fixup(self, node);
			return AO2_CONTAINER_INSERT_NODE_INSERTED;
		}
		switch (bias) {
		case BIAS_FIRST:
			/* Duplicate nodes unconditionally accepted. */
			if (cur->left) {
				cur = cur->left;
				continue;
			}

			/* Node becomes a left child */
			cur->left = node;
			node->parent = cur;
			rb_insert_fixup(self, node);
			return AO2_CONTAINER_INSERT_NODE_INSERTED;
		case BIAS_EQUAL:
			break;
		case BIAS_LAST:
			/* Duplicate nodes unconditionally accepted. */
			if (cur->right) {
				cur = cur->right;
				continue;
			}

			/* Node becomes a right child */
			cur->right = node;
			node->parent = cur;
			rb_insert_fixup(self, node);
			return AO2_CONTAINER_INSERT_NODE_INSERTED;
		}

		break;
	}

	/* Node is a dupliate */
	switch (options & AO2_CONTAINER_ALLOC_OPT_DUPS_MASK) {
	default:
	case AO2_CONTAINER_ALLOC_OPT_DUPS_ALLOW:
		ast_assert(0);/* Case already handled by BIAS_FIRST/BIAS_LAST. */
		return AO2_CONTAINER_INSERT_NODE_REJECTED;
	case AO2_CONTAINER_ALLOC_OPT_DUPS_REJECT:
		/* Reject all objects with the same key. */
		return AO2_CONTAINER_INSERT_NODE_REJECTED;
	case AO2_CONTAINER_ALLOC_OPT_DUPS_OBJ_REJECT:
		if (cur->common.obj == node->common.obj) {
			/* Reject inserting the same object */
			return AO2_CONTAINER_INSERT_NODE_REJECTED;
		}
		next = cur;
		if (options & AO2_CONTAINER_ALLOC_OPT_INSERT_BEGIN) {
			/* Search to end of duplicates for the same object. */
			for (;;) {
				next = rb_node_next_full(next);
				if (!next) {
					break;
				}
				if (next->common.obj == node->common.obj) {
					/* Reject inserting the same object */
					return AO2_CONTAINER_INSERT_NODE_REJECTED;
				}
				cmp = sort_fn(next->common.obj, node->common.obj, OBJ_SEARCH_OBJECT);
				if (cmp) {
					break;
				}
			}

			/* Find first duplicate node. */
			for (;;) {
				next = rb_node_prev_full(cur);
				if (!next) {
					break;
				}
				if (next->common.obj == node->common.obj) {
					/* Reject inserting the same object */
					return AO2_CONTAINER_INSERT_NODE_REJECTED;
				}
				cmp = sort_fn(next->common.obj, node->common.obj, OBJ_SEARCH_OBJECT);
				if (cmp) {
					break;
				}
				cur = next;
			}
			if (!cur->left) {
				/* Node becomes a left child */
				cur->left = node;
			} else {
				/* Node becomes a right child */
				cur = rb_node_most_right(cur->left);
				cur->right = node;
			}
		} else {
			/* Search to beginning of duplicates for the same object. */
			for (;;) {
				next = rb_node_prev_full(next);
				if (!next) {
					break;
				}
				if (next->common.obj == node->common.obj) {
					/* Reject inserting the same object */
					return AO2_CONTAINER_INSERT_NODE_REJECTED;
				}
				cmp = sort_fn(next->common.obj, node->common.obj, OBJ_SEARCH_OBJECT);
				if (cmp) {
					break;
				}
			}

			/* Find last duplicate node. */
			for (;;) {
				next = rb_node_next_full(cur);
				if (!next) {
					break;
				}
				if (next->common.obj == node->common.obj) {
					/* Reject inserting the same object */
					return AO2_CONTAINER_INSERT_NODE_REJECTED;
				}
				cmp = sort_fn(next->common.obj, node->common.obj, OBJ_SEARCH_OBJECT);
				if (cmp) {
					break;
				}
				cur = next;
			}
			if (!cur->right) {
				/* Node becomes a right child */
				cur->right = node;
			} else {
				/* Node becomes a left child */
				cur = rb_node_most_left(cur->right);
				cur->left = node;
			}
		}
		break;
	case AO2_CONTAINER_ALLOC_OPT_DUPS_REPLACE:
		SWAP(cur->common.obj, node->common.obj);
		ao2_t_ref(node, -1, NULL);
		return AO2_CONTAINER_INSERT_NODE_OBJ_REPLACED;
	}

	/* Complete inserting duplicate node. */
	node->parent = cur;
	rb_insert_fixup(self, node);
	return AO2_CONTAINER_INSERT_NODE_INSERTED;
}

/*!
 * \internal
 * \brief Find the next rbtree container node in a traversal.
 * \since 12.0.0
 *
 * \param self Container to operate upon.
 * \param state Traversal state to restart rbtree container traversal.
 * \param prev Previous node returned by the traversal search functions.
 *    The ref ownership is passed back to this function.
 *
 * \retval node-ptr of found node (Reffed).
 * \retval NULL when no node found.
 */
static struct rbtree_node *rb_ao2_find_next(struct ao2_container_rbtree *self, struct rbtree_traversal_state *state, struct rbtree_node *prev)
{
	struct rbtree_node *node;
	void *arg;
	enum search_flags flags;
	int cmp;

	arg = state->arg;
	flags = state->flags;

	node = prev;
	for (;;) {
		/* Find next node in traversal order. */
		switch (flags & OBJ_ORDER_MASK) {
		default:
		case OBJ_ORDER_ASCENDING:
			node = rb_node_next(node);
			break;
		case OBJ_ORDER_DESCENDING:
			node = rb_node_prev(node);
			break;
		case OBJ_ORDER_PRE:
			node = rb_node_pre(node);
			break;
		case OBJ_ORDER_POST:
			node = rb_node_post(node);
			break;
		}
		if (!node) {
			/* No more nodes left to traverse. */
			break;
		}
		if (!node->common.obj) {
			/* Node is empty */
			continue;
		}

		if (state->sort_fn) {
			/* Filter node through the sort_fn */
			cmp = state->sort_fn(node->common.obj, arg, flags & OBJ_SEARCH_MASK);
			if (cmp) {
				/* No more nodes in this container are possible to match. */
				break;
			}
		}

		/* We have the next traversal node */
		ao2_t_ref(node, +1, NULL);

		/*
		 * Dereferencing the prev node may result in our next node
		 * object being removed by another thread.  This could happen if
		 * the container uses RW locks and the container was read
		 * locked.
		 */
		ao2_t_ref(prev, -1, NULL);
		if (node->common.obj) {
			return node;
		}
		prev = node;
	}

	/* No more nodes in the container left to traverse. */
	ao2_t_ref(prev, -1, NULL);
	return NULL;
}

/*!
 * \internal
 * \brief Find an initial matching node.
 * \since 12.0.0
 *
 * \param self Container to operate upon.
 * \param obj_right pointer to the (user-defined part) of an object.
 * \param flags flags from ao2_callback()
 *   OBJ_SEARCH_OBJECT - if set, 'obj_right', is an object.
 *   OBJ_SEARCH_KEY - if set, 'obj_right', is a search key item that is not an object.
 *   OBJ_SEARCH_PARTIAL_KEY - if set, 'obj_right', is a partial search key item that is not an object.
 * \param bias How to bias search direction for duplicates
 *
 * \retval node on success.
 * \retval NULL if not found.
 */
static struct rbtree_node *rb_find_initial(struct ao2_container_rbtree *self, void *obj_right, enum search_flags flags, enum equal_node_bias bias)
{
	int cmp;
	enum search_flags sort_flags;
	struct rbtree_node *node;
	struct rbtree_node *next = NULL;
	ao2_sort_fn *sort_fn;

	sort_flags = flags & OBJ_SEARCH_MASK;
	sort_fn = self->common.sort_fn;

	/* Find node where normal search would find it. */
	node = self->root;
	if (!node) {
		return NULL;
	}
	for (;;) {
		if (!node->common.obj) {
			/* Which direction do we go to find the node? */
			if (rb_find_empty_direction(node, sort_fn, obj_right, sort_flags, bias)
				== GO_LEFT) {
				next = node->left;
			} else {
				next = node->right;
			}
			if (!next) {
				switch (bias) {
				case BIAS_FIRST:
					/* Check successor node for match. */
					next = rb_node_next_full(node);
					break;
				case BIAS_EQUAL:
					break;
				case BIAS_LAST:
					/* Check previous node for match. */
					next = rb_node_prev_full(node);
					break;
				}
				if (next) {
					cmp = sort_fn(next->common.obj, obj_right, sort_flags);
					if (cmp == 0) {
						/* Found the first/last matching node. */
						return next;
					}
					next = NULL;
				}

				/* No match found. */
				return next;
			}
		} else {
			cmp = sort_fn(node->common.obj, obj_right, sort_flags);
			if (cmp > 0) {
				next = node->left;
			} else if (cmp < 0) {
				next = node->right;
			} else {
				switch (bias) {
				case BIAS_FIRST:
					next = node->left;
					break;
				case BIAS_EQUAL:
					return node;
				case BIAS_LAST:
					next = node->right;
					break;
				}
				if (!next) {
					/* Found the first/last matching node. */
					return node;
				}
			}
			if (!next) {
				switch (bias) {
				case BIAS_FIRST:
					if (cmp < 0) {
						/* Check successor node for match. */
						next = rb_node_next_full(node);
					}
					break;
				case BIAS_EQUAL:
					break;
				case BIAS_LAST:
					if (cmp > 0) {
						/* Check previous node for match. */
						next = rb_node_prev_full(node);
					}
					break;
				}
				if (next) {
					cmp = sort_fn(next->common.obj, obj_right, sort_flags);
					if (cmp == 0) {
						/* Found the first/last matching node. */
						return next;
					}
				}

				/* No match found. */
				return NULL;
			}
		}
		node = next;
	}
}

/*!
 * \internal
 * \brief Find the first rbtree container node in a traversal.
 * \since 12.0.0
 *
 * \param self Container to operate upon.
 * \param flags search_flags to control traversing the container
 * \param arg Comparison callback arg parameter.
 * \param state Traversal state to restart rbtree container traversal.
 *
 * \retval node-ptr of found node (Reffed).
 * \retval NULL when no node found.
 */
static struct rbtree_node *rb_ao2_find_first(struct ao2_container_rbtree *self, enum search_flags flags, void *arg, struct rbtree_traversal_state *state)
{
	struct rbtree_node *node;
	enum equal_node_bias bias;

	if (self->common.destroying) {
		/* Force traversal to be post order for tree destruction. */
		flags = OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE | OBJ_ORDER_POST;
	}

	memset(state, 0, sizeof(*state));
	state->arg = arg;
	state->flags = flags;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
	case OBJ_SEARCH_KEY:
	case OBJ_SEARCH_PARTIAL_KEY:
		/* We are asked to do a directed search. */
		state->sort_fn = self->common.sort_fn;
		break;
	default:
		/* Don't know, let's visit all nodes */
		state->sort_fn = NULL;
		break;
	}

	if (!self->root) {
		/* Tree is empty. */
		return NULL;
	}

	/* Find first traversal node. */
	switch (flags & OBJ_ORDER_MASK) {
	default:
	case OBJ_ORDER_ASCENDING:
		if (!state->sort_fn) {
			/* Find left most child. */
			node = rb_node_most_left(self->root);
			if (!node->common.obj) {
				node = rb_node_next_full(node);
				if (!node) {
					return NULL;
				}
			}
			break;
		}

		/* Search for initial node. */
		switch (self->common.options & AO2_CONTAINER_ALLOC_OPT_DUPS_MASK) {
		case AO2_CONTAINER_ALLOC_OPT_DUPS_REJECT:
		case AO2_CONTAINER_ALLOC_OPT_DUPS_REPLACE:
			if ((flags & OBJ_SEARCH_MASK) != OBJ_SEARCH_PARTIAL_KEY) {
				/* There are no duplicates allowed. */
				bias = BIAS_EQUAL;
				break;
			}
			/* Fall through */
		default:
		case AO2_CONTAINER_ALLOC_OPT_DUPS_ALLOW:
		case AO2_CONTAINER_ALLOC_OPT_DUPS_OBJ_REJECT:
			/* Find first duplicate node. */
			bias = BIAS_FIRST;
			break;
		}
		node = rb_find_initial(self, arg, flags, bias);
		if (!node) {
			return NULL;
		}
		break;
	case OBJ_ORDER_DESCENDING:
		if (!state->sort_fn) {
			/* Find right most child. */
			node = rb_node_most_right(self->root);
			if (!node->common.obj) {
				node = rb_node_prev_full(node);
				if (!node) {
					return NULL;
				}
			}
			break;
		}

		/* Search for initial node. */
		switch (self->common.options & AO2_CONTAINER_ALLOC_OPT_DUPS_MASK) {
		case AO2_CONTAINER_ALLOC_OPT_DUPS_REJECT:
		case AO2_CONTAINER_ALLOC_OPT_DUPS_REPLACE:
			if ((flags & OBJ_SEARCH_MASK) != OBJ_SEARCH_PARTIAL_KEY) {
				/* There are no duplicates allowed. */
				bias = BIAS_EQUAL;
				break;
			}
			/* Fall through */
		default:
		case AO2_CONTAINER_ALLOC_OPT_DUPS_ALLOW:
		case AO2_CONTAINER_ALLOC_OPT_DUPS_OBJ_REJECT:
			/* Find last duplicate node. */
			bias = BIAS_LAST;
			break;
		}
		node = rb_find_initial(self, arg, flags, bias);
		if (!node) {
			return NULL;
		}
		break;
	case OBJ_ORDER_PRE:
		/* This is a tree structure traversal so we must visit all nodes. */
		state->sort_fn = NULL;

		node = self->root;

		/* Find a non-empty node. */
		while (!node->common.obj) {
			node = rb_node_pre(node);
			if (!node) {
				return NULL;
			}
		}
		break;
	case OBJ_ORDER_POST:
		/* This is a tree structure traversal so we must visit all nodes. */
		state->sort_fn = NULL;

		/* Find the left most childless node. */
		node = self->root;
		for (;;) {
			node = rb_node_most_left(node);
			if (!node->right) {
				/* This node has no children. */
				break;
			}
			node = node->right;
		}

		/* Find a non-empty node. */
		while (!node->common.obj) {
			node = rb_node_post(node);
			if (!node) {
				return NULL;
			}
		}
		break;
	}

	/* We have the first traversal node */
	ao2_t_ref(node, +1, NULL);
	return node;
}

/*!
 * \internal
 * \brief Find the next non-empty iteration node in the container.
 * \since 12.0.0
 *
 * \param self Container to operate upon.
 * \param node Previous node returned by the iterator.
 * \param flags search_flags to control iterating the container.
 *   Only AO2_ITERATOR_DESCENDING is useful by the method.
 *
 * \note The container is already locked.
 *
 * \retval node on success.
 * \retval NULL on error or no more nodes in the container.
 */
static struct rbtree_node *rb_ao2_iterator_next(struct ao2_container_rbtree *self, struct rbtree_node *node, enum ao2_iterator_flags flags)
{
	if (flags & AO2_ITERATOR_DESCENDING) {
		if (!node) {
			/* Find right most node. */
			if (!self->root) {
				return NULL;
			}
			node = rb_node_most_right(self->root);
			if (node->common.obj) {
				/* Found a non-empty node. */
				return node;
			}
		}
		/* Find next non-empty node. */
		node = rb_node_prev_full(node);
	} else {
		if (!node) {
			/* Find left most node. */
			if (!self->root) {
				return NULL;
			}
			node = rb_node_most_left(self->root);
			if (node->common.obj) {
				/* Found a non-empty node. */
				return node;
			}
		}
		/* Find next non-empty node. */
		node = rb_node_next_full(node);
	}

	return node;
}

/*!
 * \internal
 *
 * \brief Destroy this container.
 * \since 12.0.0
 *
 * \param self Container to operate upon.
 *
 * \return Nothing
 */
static void rb_ao2_destroy(struct ao2_container_rbtree *self)
{
	/* Check that the container no longer has any nodes */
	if (self->root) {
		ast_log(LOG_ERROR, "Node ref leak.  Red-Black tree container still has nodes!\n");
		ast_assert(0);
	}
}

#if defined(AO2_DEBUG)
/*!
 * \internal
 * \brief Display contents of the specified container.
 * \since 12.0.0
 *
 * \param self Container to dump.
 * \param where User data needed by prnt to determine where to put output.
 * \param prnt Print output callback function to use.
 * \param prnt_obj Callback function to print the given object's key. (NULL if not available)
 *
 * \return Nothing
 */
static void rb_ao2_dump(struct ao2_container_rbtree *self, void *where, ao2_prnt_fn *prnt, ao2_prnt_obj_fn *prnt_obj)
{
#define FORMAT  "%16s, %16s, %16s, %16s, %5s, %16s, %s\n"
#define FORMAT2 "%16p, %16p, %16p, %16p, %5s, %16p, "

	struct rbtree_node *node;

	prnt(where, FORMAT, "Node", "Parent", "Left", "Right", "Color", "Obj", "Key");
	for (node = self->root; node; node = rb_node_pre(node)) {
		prnt(where, FORMAT2,
			node,
			node->parent,
			node->left,
			node->right,
			node->is_red ? "Red" : "Black",
			node->common.obj);
		if (node->common.obj && prnt_obj) {
			prnt_obj(node->common.obj, where, prnt);
		}
		prnt(where, "\n");
	}

#undef FORMAT
#undef FORMAT2
}
#endif	/* defined(AO2_DEBUG) */

#if defined(AO2_DEBUG)
/*!
 * \internal
 * \brief Display statistics of the specified container.
 * \since 12.0.0
 *
 * \param self Container to display statistics.
 * \param where User data needed by prnt to determine where to put output.
 * \param prnt Print output callback function to use.
 *
 * \note The container is already locked for reading.
 *
 * \return Nothing
 */
static void rb_ao2_stats(struct ao2_container_rbtree *self, void *where, ao2_prnt_fn *prnt)
{
	int idx;

	for (idx = 0; idx < ARRAY_LEN(self->stats.fixup_insert_left); ++idx) {
		prnt(where, "Number of left insert fixups case %d: %d\n", idx + 1,
			self->stats.fixup_insert_left[idx]);
	}
	for (idx = 0; idx < ARRAY_LEN(self->stats.fixup_insert_right); ++idx) {
		prnt(where, "Number of right insert fixups case %d: %d\n", idx + 1,
			self->stats.fixup_insert_right[idx]);
	}

	for (idx = 0; idx < ARRAY_LEN(self->stats.delete_children); ++idx) {
		prnt(where, "Number of nodes deleted with %d children: %d\n", idx,
			self->stats.delete_children[idx]);
	}
	for (idx = 0; idx < ARRAY_LEN(self->stats.fixup_delete_left); ++idx) {
		prnt(where, "Number of left delete fixups case %d: %d\n", idx + 1,
			self->stats.fixup_delete_left[idx]);
	}
	for (idx = 0; idx < ARRAY_LEN(self->stats.fixup_delete_right); ++idx) {
		prnt(where, "Number of right delete fixups case %d: %d\n", idx + 1,
			self->stats.fixup_delete_right[idx]);
	}
}
#endif	/* defined(AO2_DEBUG) */

#if defined(AO2_DEBUG)
/*!
 * \internal
 * \brief Check the black height of the given node.
 * \since 12.0.0
 *
 * \param node Node to check black height.
 *
 * \retval black-height of node on success.
 * \retval -1 on error.  Node black height did not balance.
 */
static int rb_check_black_height(struct rbtree_node *node)
{
	int height_left;
	int height_right;

	if (!node) {
		/* A NULL child is a black node. */
		return 0;
	}

	height_left = rb_check_black_height(node->left);
	if (height_left < 0) {
		return -1;
	}
	height_right = rb_check_black_height(node->right);
	if (height_right < 0) {
		return -1;
	}
	if (height_left != height_right) {
		ast_log(LOG_ERROR,
			"Tree node black height of children does not match! L:%d != R:%d\n",
			height_left, height_right);
		return -1;
	}
	if (!node->is_red) {
		/* The node itself is black. */
		++height_left;
	}
	return height_left;
}

#endif	/* defined(AO2_DEBUG) */

#if defined(AO2_DEBUG)
/*!
 * \internal
 * \brief Perform an integrity check on the specified container.
 * \since 12.0.0
 *
 * \param self Container to check integrity.
 *
 * \note The container is already locked for reading.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int rb_ao2_integrity(struct ao2_container_rbtree *self)
{
	int res;
	int count_node;
	int count_obj;
	void *obj_last;
	struct rbtree_node *node;

	res = 0;

	count_node = 0;
	count_obj = 0;

	/*
	 * See the properties listed at struct rbtree_node definition.
	 *
	 * The rbtree properties 1 and 3 are not testable.
	 *
	 * Property 1 is not testable because we are not rebalancing at
	 * this time so all nodes are either red or black.
	 *
	 * Property 3 is not testable because it is the definition of a
	 * NULL child.
	 */
	if (self->root) {
		/* Check tree links. */
		if (self->root->parent) {
			if (self->root->parent == self->root) {
				ast_log(LOG_ERROR, "Tree root parent pointer points to itself!\n");
			} else {
				ast_log(LOG_ERROR, "Tree root is not a root node!\n");
			}
			return -1;
		}
		if (self->root->is_red) {
			/* Violation rbtree property 2. */
			ast_log(LOG_ERROR, "Tree root is red!\n");
			res = -1;
		}
		node = self->root;
		do {
			if (node->left) {
				if (node->left == node) {
					ast_log(LOG_ERROR, "Tree node's left pointer points to itself!\n");
					return -1;
				}
				if (node->left->parent != node) {
					ast_log(LOG_ERROR, "Tree node's left child does not link back!\n");
					return -1;
				}
			}
			if (node->right) {
				if (node->right == node) {
					ast_log(LOG_ERROR, "Tree node's right pointer points to itself!\n");
					return -1;
				}
				if (node->right->parent != node) {
					ast_log(LOG_ERROR, "Tree node's right child does not link back!\n");
					return -1;
				}
			}

			/* Check red/black node flags. */
			if (node->is_red) {
				/* A red node must have two black children or no children. */
				if (node->left && node->right) {
					/* Node has two children. */
					if (node->left->is_red) {
						/* Violation rbtree property 4. */
						ast_log(LOG_ERROR, "Tree node is red and its left child is red!\n");
						res = -1;
					}
					if (node->right->is_red) {
						/* Violation rbtree property 4. */
						ast_log(LOG_ERROR, "Tree node is red and its right child is red!\n");
						res = -1;
					}
				} else if (node->left || node->right) {
					/*
					 * Violation rbtree property 4 if the child is red.
					 * Violation rbtree property 5 if the child is black.
					 */
					ast_log(LOG_ERROR, "Tree node is red and it only has one child!\n");
					res = -1;
				}
			} else {
				/*
				 * A black node must have two children, or one red child, or no
				 * children.  If the black node has two children and only one of
				 * them is red, that red child must have two children.
				 */
				if (node->left && node->right) {
					/* Node has two children. */
					if (node->left->is_red != node->right->is_red) {
						/* The children are not the same color. */
						struct rbtree_node *red;

						if (node->left->is_red) {
							red = node->left;
						} else {
							red = node->right;
						}
						if (!red->left || !red->right) {
							/* Violation rbtree property 5. */
							ast_log(LOG_ERROR,
								"Tree node is black and the red child does not have two children!\n");
							res = -1;
						}
					}
				} else if ((node->left && !node->left->is_red)
					|| (node->right && !node->right->is_red)) {
					/* Violation rbtree property 5. */
					ast_log(LOG_ERROR, "Tree node is black and its only child is black!\n");
					res = -1;
				}
			}

			/* Count nodes and objects. */
			++count_node;
			if (node->common.obj) {
				++count_obj;
			}

			node = rb_node_pre(node);
		} while (node);

		/* Check node key sort order. */
		obj_last = NULL;
		for (node = rb_node_most_left(self->root); node; node = rb_node_next(node)) {
			if (!node->common.obj) {
				/* Node is empty. */
				continue;
			}

			if (obj_last) {
				if (self->common.sort_fn(obj_last, node->common.obj, OBJ_SEARCH_OBJECT) > 0) {
					ast_log(LOG_ERROR, "Tree nodes are out of sorted order!\n");
					return -1;
				}
			}
			obj_last = node->common.obj;
		}

		/* Completely check property 5 */
		if (!res && rb_check_black_height(self->root) < 0) {
			/* Violation rbtree property 5. */
			res = -1;
		}
	}

	/* Check total obj count. */
	if (count_obj != ao2_container_count(&self->common)) {
		ast_log(LOG_ERROR, "Total object count does not match ao2_container_count()!\n");
		return -1;
	}

	/* Check total node count. */
	if (count_node != self->common.nodes) {
		ast_log(LOG_ERROR, "Total node count of %d does not match stat of %d!\n",
			count_node, self->common.nodes);
		return -1;
	}

	return res;
}
#endif	/* defined(AO2_DEBUG) */

/*! rbtree container virtual method table. */
static const struct ao2_container_methods v_table_rbtree = {
	.alloc_empty_clone = (ao2_container_alloc_empty_clone_fn) rb_ao2_alloc_empty_clone,
	.new_node = (ao2_container_new_node_fn) rb_ao2_new_node,
	.insert = (ao2_container_insert_fn) rb_ao2_insert_node,
	.traverse_first = (ao2_container_find_first_fn) rb_ao2_find_first,
	.traverse_next = (ao2_container_find_next_fn) rb_ao2_find_next,
	.iterator_next = (ao2_iterator_next_fn) rb_ao2_iterator_next,
	.destroy = (ao2_container_destroy_fn) rb_ao2_destroy,
#if defined(AO2_DEBUG)
	.dump = (ao2_container_display) rb_ao2_dump,
	.stats = (ao2_container_statistics) rb_ao2_stats,
	.integrity = (ao2_container_integrity) rb_ao2_integrity,
#endif	/* defined(AO2_DEBUG) */
};

/*!
 * \brief Initialize a rbtree container.
 *
 * \param self Container to initialize.
 * \param options Container behaviour options (See enum ao2_container_opts)
 * \param sort_fn Pointer to a sort function.
 * \param cmp_fn Pointer to a compare function used by ao2_find.
 *
 * \return A pointer to a struct container.
 */
static struct ao2_container *rb_ao2_container_init(struct ao2_container_rbtree *self,
	unsigned int options, ao2_sort_fn *sort_fn, ao2_callback_fn *cmp_fn)
{
	if (!self) {
		return NULL;
	}

	self->common.v_table = &v_table_rbtree;
	self->common.sort_fn = sort_fn;
	self->common.cmp_fn = cmp_fn;
	self->common.options = options;

#ifdef AO2_DEBUG
	ast_atomic_fetchadd_int(&ao2.total_containers, 1);
#endif	/* defined(AO2_DEBUG) */

	return (struct ao2_container *) self;
}

struct ao2_container *__ao2_container_alloc_rbtree(unsigned int ao2_options, unsigned int container_options,
	ao2_sort_fn *sort_fn, ao2_callback_fn *cmp_fn,
	const char *tag, const char *file, int line, const char *func)
{
	struct ao2_container_rbtree *self;

	if (!sort_fn) {
		/* Sanity checks. */
		ast_log(__LOG_ERROR, file, line, func, "Missing sort_fn()!\n");
		return NULL;
	}

	self = __ao2_alloc(sizeof(*self), container_destruct, ao2_options,
		tag ?: __PRETTY_FUNCTION__, file, line, func);
	return rb_ao2_container_init(self, container_options, sort_fn, cmp_fn);
}
