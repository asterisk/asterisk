"""ignore 183 without sdp

Revision ID: 80473bad3c16
Revises: f3c0b8695b66
Create Date: 2019-03-04 08:30:51.592907

"""

# revision identifiers, used by Alembic.
revision = '80473bad3c16'
down_revision = 'f3c0b8695b66'

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

    op.add_column('ps_endpoints', sa.Column('ignore_183_without_sdp', ast_bool_values))

def downgrade():
    if op.get_context().bind.dialect.name == 'mssql':
        op.drop_constraint('ck_ps_endpoints_ignore_183_without_sdp_ast_bool_values', 'ps_endpoints')
    op.drop_column('ps_endpoints', 'ignore_183_without_sdp')
    pass
