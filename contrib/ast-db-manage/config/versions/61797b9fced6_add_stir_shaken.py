"""add stir shaken

Revision ID: 61797b9fced6
Revises: fbb7766f17bc
Create Date: 2020-06-29 11:52:59.946929

"""

# revision identifiers, used by Alembic.
revision = '61797b9fced6'
down_revision = 'b80485ff4dd0'

from alembic import op
import sqlalchemy as sa
from sqlalchemy.dialects.postgresql import ENUM

YESNO_NAME = 'yesno_values'
YESNO_VALUES = ['yes', 'no']

AST_BOOL_NAME = 'ast_bool_values'
AST_BOOL_VALUES = [ '0', '1',
                    'off', 'on',
                    'false', 'true',
                    'no', 'yes' ]

def upgrade():
    ast_bool_values = ENUM(*AST_BOOL_VALUES, name=AST_BOOL_NAME, create_type=False)
    op.add_column('ps_endpoints', sa.Column('stir_shaken', ast_bool_values))

def downgrade():
    op.drop_column('ps_endpoints', 'stir_shaken')
