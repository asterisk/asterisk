"""pjsip add send_contact_status_on_update_registration

Revision ID: 0838f8db6a61
Revises: 1ac563b350a8
Create Date: 2018-12-18 14:45:07.811415

"""

# revision identifiers, used by Alembic.
revision = '0838f8db6a61'
down_revision = '1ac563b350a8'

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

    op.add_column('ps_globals', sa.Column('send_contact_status_on_update_registration', ast_bool_values))


def downgrade():
    if op.get_context().bind.dialect.name == 'mssql':
        op.drop_constraint('ck_ps_globals_send_contact_status_on_update_registration_ast_bool_values', 'ps_globals')
    op.drop_column('ps_globals', 'send_contact_status_on_update_registration')
