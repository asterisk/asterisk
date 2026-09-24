"""add timers_refresher to ps_endpoints

Revision ID: 1e65f5e27981
Revises: 2b45fd748a4f
Create Date: 2026-09-05 10:30:00.000000

"""

# revision identifiers, used by Alembic.
revision = '1e65f5e27981'
down_revision = '2b45fd748a4f'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.add_column('ps_endpoints', sa.Column('timers_refresher', sa.String(40)))


def downgrade():
    op.drop_column('ps_endpoints', 'timers_refresher')
