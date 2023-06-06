"""all_codecs_on_empty_reinvite

Revision ID: ccf795ee535f
Revises: 417c0247fd7e
Create Date: 2022-09-28 09:14:36.709781

"""

# revision identifiers, used by Alembic.
revision = 'ccf795ee535f'
down_revision = '417c0247fd7e'

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
    ############################# Enums ##############################

    # ast_bool_values has already been created, so use postgres enum object
    # type to get around "already created" issue - works okay with mysql
    ast_bool_values = ENUM(*AST_BOOL_VALUES, name=AST_BOOL_NAME, create_type=False)

    op.add_column('ps_globals', sa.Column('all_codecs_on_empty_reinvite', ast_bool_values))

def downgrade():
    if op.get_context().bind.dialect.name == 'mssql':
        op.drop_constraint('ck_ps_globals_all_codecs_on_empty_reinvite_ast_bool_values', 'ps_globals')
    op.drop_column('ps_globals', 'all_codecs_on_empty_reinvite')
