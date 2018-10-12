"""add pjsip trust/send _connected_line

Revision ID: 1ac563b350a8
Revises: 2bb1a85135ad
Create Date: 2018-10-12 17:10:34.530282

"""

# revision identifiers, used by Alembic.
revision = '1ac563b350a8'
down_revision = '2bb1a85135ad'

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

    op.add_column('ps_endpoints', sa.Column('trust_connected_line', ast_bool_values))
    op.add_column('ps_endpoints', sa.Column('send_connected_line', ast_bool_values))

def downgrade():
    if op.get_context().bind.dialect.name == 'mssql':
        op.drop_constraint('ck_ps_endpoints_trust_connected_line_ast_bool_values', 'ps_endpoints')
        op.drop_constraint('ck_ps_endpoints_send_connected_line_ast_bool_values', 'ps_endpoints')
    op.drop_column('ps_endpoints', 'trust_connected_line')
    op.drop_column('ps_endpoints', 'send_connected_line')
