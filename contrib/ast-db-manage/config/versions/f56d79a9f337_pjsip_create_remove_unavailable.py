"""pjsip create remove_unavailable

Revision ID: f56d79a9f337
Revises: c20d6e3992f4
Create Date: 2021-07-28 02:09:11.082061

"""

# revision identifiers, used by Alembic.
revision = 'f56d79a9f337'
down_revision = 'c20d6e3992f4'

from alembic import op
import sqlalchemy as sa
from sqlalchemy.dialects.postgresql import ENUM

AST_BOOL_NAME = 'ast_bool_values'
AST_BOOL_VALUES = [ '0', '1',
                    'off', 'on',
                    'false', 'true',
                    'no', 'yes' ]

def upgrade():
    ast_bool_values = ENUM(*AST_BOOL_VALUES, name=AST_BOOL_NAME, create_type=False)
    op.add_column('ps_aors', sa.Column('remove_unavailable', ast_bool_values))

def downgrade():
    op.drop_column('ps_aors', 'remove_unavailable')
    pass

