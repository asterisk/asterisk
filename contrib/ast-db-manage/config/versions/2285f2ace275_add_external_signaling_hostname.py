"""Add external_signaling_hostname to ps_transports

Revision ID: 2285f2ace275
Revises: e89e30cee53f
Create Date: 2026-06-03

"""

# revision identifiers, used by Alembic.
revision = '2285f2ace275'
down_revision = 'e89e30cee53f'

from alembic import op
import sqlalchemy as sa

def upgrade():
    op.add_column('ps_transports', sa.Column('external_signaling_hostname', sa.String(40)))

def downgrade():
    op.drop_column('ps_transports', 'external_signaling_hostname')