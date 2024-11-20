"""Add rtt options

Revision ID: 65b5ac60e54a
Revises: abdc9ede147d
Create Date: 2025-02-21 12:35:43.615049

"""

# revision identifiers, used by Alembic.
revision = '65b5ac60e54a'
down_revision = 'abdc9ede147d'

from alembic import op
import sqlalchemy as sa

def upgrade():
    op.add_column('ps_endpoints', sa.Column('tos_text', sa.String(10)))
    op.add_column('ps_endpoints', sa.Column('cos_text', sa.Integer))
    op.add_column('ps_endpoints', sa.Column('max_text_streams', sa.Integer))

def downgrade():
    op.drop_column('ps_endpoints', 'tos_text')
    op.drop_column('ps_endpoints', 'cos_text')
    op.drop_column('ps_endpoints', 'max_text_streams')
    