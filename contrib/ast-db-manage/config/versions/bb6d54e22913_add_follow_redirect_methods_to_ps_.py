"""add follow_redirect_methods to ps_endpoints

Revision ID: bb6d54e22913
Revises: dc7c357dc178 
Create Date: 2025-12-12 11:26:36.591932

"""

# revision identifiers, used by Alembic.
revision = 'bb6d54e22913'
down_revision = 'dc7c357dc178'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.add_column('ps_endpoints', sa.Column('follow_redirect_methods', sa.String(95)))


def downgrade():
    op.drop_column('ps_endpoints', 'follow_redirect_methods')
