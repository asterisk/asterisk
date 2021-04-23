"""add allow_unauthenticated_options

Revision ID: c20d6e3992f4
Revises: 8915fcc5766f
Create Date: 2021-04-23 13:44:38.296558

"""

# revision identifiers, used by Alembic.
revision = 'c20d6e3992f4'
down_revision = '8915fcc5766f'

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
    op.add_column('ps_endpoints', sa.Column('allow_unauthenticated_options', ast_bool_values))

def downgrade():
    op.drop_column('ps_endpoints', 'allow_unauthenticated_options')
    pass
