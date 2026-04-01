"""add rtp_port_start and rtp_port_end to ps_endpoints

Revision ID: e89e30cee53f
Revises: bb6d54e22913
Create Date: 2026-04-02 10:00:00.000000

"""

# revision identifiers, used by Alembic.
revision = 'e89e30cee53f'
down_revision = 'bb6d54e22913'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.add_column('ps_endpoints',
                  sa.Column('rtp_port_start', sa.Integer))
    op.add_column('ps_endpoints',
                  sa.Column('rtp_port_end', sa.Integer))


def downgrade():
    op.drop_column('ps_endpoints', 'rtp_port_end')
    op.drop_column('ps_endpoints', 'rtp_port_start')
