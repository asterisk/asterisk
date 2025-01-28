"""add qualify 2xx only option

Revision ID: 44bd6dd914fa
Revises: 4f91fc18c979
Create Date: 2024-12-02 21:08:41.130023
Update Date: 2025-01-28 09:50:00.000000
"""

# revision identifiers, used by Alembic.
revision = '44bd6dd914fa'
down_revision = '4f91fc18c979'

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
    op.add_column('ps_aors', sa.Column('qualify_2xx_only', ast_bool_values))
    op.add_column('ps_contacts', sa.Column('qualify_2xx_only', ast_bool_values))


def downgrade():
    if op.get_context().bind.dialect.name == 'mssql':
        op.drop_constraint('ck_ps_aors_qualify_2xx_only_ast_bool_values', 'ps_aors')
        op.drop_constraint('ck_ps_contacts_qualify_2xx_only_ast_bool_values', 'ps_contacts')
    op.drop_column('ps_aors', 'qualify_2xx_only')
    op.drop_column('ps_contacts', 'qualify_2xx_only')
