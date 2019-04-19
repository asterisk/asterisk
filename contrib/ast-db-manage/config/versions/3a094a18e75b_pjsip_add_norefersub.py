"""pjsip add norefersub

Revision ID: 3a094a18e75b
Revises: 80473bad3c16
Create Date: 2019-04-17 09:25:42.040269

"""

# revision identifiers, used by Alembic.
revision = '3a094a18e75b'
down_revision = '80473bad3c16'

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
    ast_bool_values = ENUM(*AST_BOOL_VALUES, name=AST_BOOL_NAME, create_type=True)

    op.add_column('ps_globals', sa.Column('norefersub', ast_bool_values))


def downgrade():
    if op.get_context().bind.dialect.name == 'mssql':
        op.drop_constraint('ck_ps_globals_norefersub_ast_bool_values', 'ps_globals')
    op.drop_column('ps_globals', 'norefersub')
