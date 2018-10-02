"""pjsip add use_callerid_contact

Revision ID: 2bb1a85135ad
Revises: 7f85dd44c775
Create Date: 2018-10-18 15:13:40.462354

"""

# revision identifiers, used by Alembic.
revision = '2bb1a85135ad'
down_revision = '7f85dd44c775'

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


    op.add_column('ps_globals', sa.Column('use_callerid_contact', ast_bool_values))


def downgrade():
    if op.get_context().bind.dialect.name == 'mssql':
        op.drop_constraint('ck_ps_globals_use_callerid_contact_ast_bool_values','ps_globals')
    op.drop_column('ps_globals', 'use_callerid_contact')

    if op.get_context().bind.dialect.name == 'postgresql':
        ENUM(name=AST_BOOL_NAME).drop(op.get_bind(), checkfirst=False)
