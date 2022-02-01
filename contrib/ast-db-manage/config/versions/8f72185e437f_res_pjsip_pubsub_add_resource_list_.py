"""res_pjsip_pubsub add resource_list option resource_display_name

Revision ID: 8f72185e437f
Revises: a06d8f8462d9
Create Date: 2022-02-01 10:53:55.875438

"""

# revision identifiers, used by Alembic.
revision = '8f72185e437f'
down_revision = 'a06d8f8462d9'

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
    op.add_column('ps_resource_list', sa.Column('resource_display_name', ast_bool_values))

def downgrade():
    op.drop_column('ps_resource_list', 'resource_display_name')

