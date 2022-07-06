"""allow_sending_180_after_183

Revision ID: 0bee61aa9425
Revises: 8f72185e437f
Create Date: 2022-04-07 13:51:33.400664

"""
from alembic import op
import sqlalchemy as sa
from sqlalchemy.dialects.postgresql import ENUM

# revision identifiers, used by Alembic.
revision = '0bee61aa9425'
down_revision = '8f72185e437f'
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

    op.add_column('ps_globals', sa.Column('allow_sending_180_after_183', ast_bool_values))


def downgrade():
    if op.get_context().bind.dialect.name == 'mssql':
        op.drop_constraint('ck_ps_globals_allow_sending_180_after_183_ast_bool_values', 'ps_globals')
    op.drop_column('ps_globals', 'allow_sending_180_after_183')
