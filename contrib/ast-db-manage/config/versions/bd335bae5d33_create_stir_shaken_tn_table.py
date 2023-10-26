"""Create STIR/SHAKEN TN table

Revision ID: bd335bae5d33
Revises: 24c12d8e9014
Create Date: 2024-01-09 12:17:47.353533

"""

# revision identifiers, used by Alembic.
revision = 'bd335bae5d33'
down_revision = '24c12d8e9014'

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
    op.create_table(
        'stir_tn',
        sa.Column('id', sa.String(80), nullable=False, primary_key=True),
        sa.Column('private_key_file', sa.String(1024), nullable=True),
        sa.Column('public_cert_url', sa.String(1024), nullable=True),
        sa.Column('attest_level', sa.String(1), nullable=True),
        sa.Column('send_mky', ast_bool_values)
    )

def downgrade():
    op.drop_table('stir_tn')
