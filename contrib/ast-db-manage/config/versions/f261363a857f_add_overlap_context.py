"""add overlap_context

Revision ID: f261363a857f
Revises: 5a2247c957d2
Create Date: 2022-12-09 13:58:48.622000

"""

# revision identifiers, used by Alembic.
revision = 'f261363a857f'
down_revision = '5a2247c957d2'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.add_column('ps_endpoints', sa.Column('overlap_context', sa.String(80)))

def downgrade():
    op.drop_column('ps_endpoints', 'overlap_context')
