"""Add suppress_moh_on_sendonly

Revision ID: 4f91fc18c979
Revises: 801b9fced8b7
Create Date: 2024-11-05 11:37:33.604448

"""

# revision identifiers, used by Alembic.
revision = '4f91fc18c979'
down_revision = '801b9fced8b7'

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
    op.add_column('ps_endpoints', sa.Column('suppress_moh_on_sendonly', ast_bool_values))

def downgrade():
    if op.get_context().bind.dialect.name == 'mssql':
        op.drop_constraint('ck_ps_endpoints_suppress_moh_on_sendonly_ast_bool_values', 'ps_endpoints')
    op.drop_column('ps_endpoints', 'suppress_moh_on_sendonly')
