/*
 * asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2014, Digium, Inc.
 *
 * Mark Michelson <mmichelson@digium.com>
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

/*!
 * PIDF state
 */
enum ast_sip_pidf_state {
	/*! Device is not in use */
	NOTIFY_OPEN,
	/*! Device is in use or ringing */
	NOTIFY_INUSE,
	/*! Device is unavailable, on hold, or busy */
	NOTIFY_CLOSED
};

/*!
 * \brief Replace offensive XML characters with XML entities
 *
 * " = &quot;
 * < = &lt;
 * > = &gt;
 * ' = &apos;
 * & = &amp;
 *
 * \param input String to sanitize
 * \param[out] output Sanitized string
 * \param len Size of output buffer
 */
void ast_sip_sanitize_xml(const char *input, char *output, size_t len);

/*!
 * \brief Convert extension state to relevant PIDF strings
 *
 * \param state The extension state
 * \param[out] statestring
 * \param[out] pidfstate
 * \param[out] pidfnote
 * \param[out] local_state
 */
void ast_sip_presence_exten_state_to_str(int state, char **statestring,
		char **pidfstate, char **pidfnote, enum ast_sip_pidf_state *local_state);

/*!
 * \brief Create XML attribute
 *
 * \param pool Allocation pool
 * \param node Node to add attribute to
 * \param name The attribute name
 * \param value The attribute value
 *
 * \return The created attribute
 */
pj_xml_attr *ast_sip_presence_xml_create_attr(pj_pool_t *pool,
		pj_xml_node *node, const char *name, const char *value);

/*!
 * \brief Create XML node
 *
 * \param pool Allocation pool
 * \param parent Node that will be parent to the created node
 * \param name The name for the new node
 * \return The created node
 */
pj_xml_node *ast_sip_presence_xml_create_node(pj_pool_t *pool,
		pj_xml_node *parent, const char* name);

/*!
 * \brief Find an attribute within a given node
 *
 * Given a starting node, this will find an attribute that belongs
 * to a specific node. If the node does not exist, it will be created
 * under the passed-in parent. If the attribute does not exist, then
 * it will be created on the node with an empty string as its value.
 *
 * \param pool Allocation pool
 * \param parent Starting node for search
 * \param node_name Name of node where to find attribute
 * \param attr_name Name of attribute to find
 * \param[out] node Node that was found or created
 * \param[out] attr Attribute that was found or created
 * \return The found attribute
 */
void ast_sip_presence_xml_find_node_attr(pj_pool_t* pool,
		pj_xml_node *parent, const char *node_name, const char *attr_name,
		pj_xml_node **node, pj_xml_attr **attr);
