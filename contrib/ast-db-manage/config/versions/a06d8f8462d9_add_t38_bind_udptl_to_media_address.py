"""add t38_bind_udptl_to_media_address

Revision ID: a06d8f8462d9
Revises: f56d79a9f337
Create Date: 2021-09-24 10:03:01.320480

"""

# revision identifiers, used by Alembic.
revision = 'a06d8f8462d9'
down_revision = 'f56d79a9f337'

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
    op.add_column('ps_endpoints', sa.Column('t38_bind_udptl_to_media_address', ast_bool_values))


def downgrade():
    op.drop_column('ps_endpoints', 't38_bind_udptl_to_media_address')
