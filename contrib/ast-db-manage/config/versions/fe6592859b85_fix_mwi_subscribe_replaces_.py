"""Fix mwi_subscribe_replaces_unsolicited

Revision ID: fe6592859b85
Revises: 1d3ed26d9978
Create Date: 2018-08-06 15:50:44.405534

"""

# revision identifiers, used by Alembic.
revision = 'fe6592859b85'
down_revision = '1d3ed26d9978'

from alembic import op
import sqlalchemy as sa
from sqlalchemy.dialects.postgresql import ENUM

AST_BOOL_NAME = 'ast_bool_values'
# We'll just ignore the n/y and f/t abbreviations as Asterisk does not write
# those aliases.
AST_BOOL_VALUES = [ '0', '1',
                    'off', 'on',
                    'false', 'true',
                    'no', 'yes' ]


def upgrade():
    # Create the new enum
    ast_bool_values = ENUM(*AST_BOOL_VALUES, name=AST_BOOL_NAME, create_type=False)
    if op.get_context().bind.dialect.name == 'postgresql':
        ast_bool_values.create(op.get_bind(), checkfirst=False)

    # There is no direct way to convert from Integer to ENUM that is
    # not database specific so we transition through a string type.
    op.alter_column('ps_endpoints', 'mwi_subscribe_replaces_unsolicited',
                    type_=sa.String(5))
    op.alter_column('ps_endpoints', 'mwi_subscribe_replaces_unsolicited',
            type_=ast_bool_values, postgresql_using='mwi_subscribe_replaces_unsolicited::{0}'.format(AST_BOOL_NAME))


def downgrade():
    # First we need to ensure the column is using only the 'numeric' bool enum values.
    op.execute("UPDATE ps_endpoints SET mwi_subscribe_replaces_unsolicited='0'"
               " WHERE mwi_subscribe_replaces_unsolicited='off'"
               "  OR mwi_subscribe_replaces_unsolicited='false'"
               "  OR mwi_subscribe_replaces_unsolicited='no'")
    op.execute("UPDATE ps_endpoints SET mwi_subscribe_replaces_unsolicited='1'"
               " WHERE mwi_subscribe_replaces_unsolicited='on'"
               "  OR mwi_subscribe_replaces_unsolicited='true'"
               "  OR mwi_subscribe_replaces_unsolicited='yes'")

    # There is no direct way to convert from ENUM to Integer that is
    # not database specific so we transition through a string type.
    if op.get_context().bind.dialect.name == 'mssql':
        op.drop_constraint('ck_ps_endpoints_mwi_subscribe_replaces_unsolicited_ast_bool_values', 'ps_endpoints')
    op.alter_column('ps_endpoints', 'mwi_subscribe_replaces_unsolicited',
                    type_=sa.String(5))
    op.alter_column('ps_endpoints', 'mwi_subscribe_replaces_unsolicited',
            type_=sa.Integer, postgresql_using='mwi_subscribe_replaces_unsolicited::Integer')

    if op.get_context().bind.dialect.name == 'postgresql':
        ENUM(name=AST_BOOL_NAME).drop(op.get_bind(), checkfirst=False)
