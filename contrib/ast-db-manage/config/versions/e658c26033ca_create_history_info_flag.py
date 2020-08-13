"""create history info flag

Revision ID: e658c26033ca
Revises: 1ae0609b6646
Create Date: 2020-08-13 10:53:21.032591

"""

# revision identifiers, used by Alembic.
revision = 'e658c26033ca'
down_revision = '1ae0609b6646'

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

    op.add_column('ps_endpoints', sa.Column('send_history_info', ast_bool_values))


def downgrade():
    if op.get_context().bind.dialect.name == 'mssql':
        op.drop_constraint('ck_ps_endpoints_send_history_info_ast_bool_values','ps_endpoints')
    op.drop_column('ps_endpoints', 'send_history_info')
