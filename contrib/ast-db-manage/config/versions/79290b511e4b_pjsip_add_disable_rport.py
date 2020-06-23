"""pjsip add disable_rport

Revision ID: 79290b511e4b
Revises: fbb7766f17bc
Create Date: 2020-06-25 22:21:37.529880

"""

# revision identifiers, used by Alembic.
revision = '79290b511e4b'
down_revision = 'fbb7766f17bc'

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

    op.add_column('ps_systems', sa.Column('disable_rport', ast_bool_values))


def downgrade():
    if op.get_context().bind.dialect.name == 'mssql':
        op.drop_constraint('ck_ps_systems_disable_rport_ast_bool_values','ps_systems')
    op.drop_column('ps_systems', 'disable_rport')
