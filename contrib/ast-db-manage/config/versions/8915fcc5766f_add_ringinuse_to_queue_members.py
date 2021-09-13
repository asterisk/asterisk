"""Add ringinuse to queue_members

Revision ID: 8915fcc5766f
Revises: e658c26033ca
Create Date: 2021-03-24 09:28:46.901447

"""

# revision identifiers, used by Alembic.
revision = '8915fcc5766f'
down_revision = 'e658c26033ca'

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
    op.add_column('queue_members', sa.Column('ringinuse', ast_bool_values))

def downgrade():
    if op.get_context().bind.dialect.name == 'mssql':
        op.drop_constraint('ck_queue_members_ringinuse_ast_bool_values', 'queue_members')
    op.drop_column('queue_members', 'ringinuse')
