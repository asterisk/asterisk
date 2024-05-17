"""Revert d5122576cca8 add transport attribute to identify

Revision ID: bd9c5159c7ea
Revises: 6c475a93f48a
Create Date: 2024-05-17 08:30:58.299083

"""

# revision identifiers, used by Alembic.
revision = 'bd9c5159c7ea'
down_revision = '6c475a93f48a'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.drop_column('ps_endpoint_id_ips', 'transport')


def downgrade():
    op.add_column('ps_endpoint_id_ips', sa.Column('transport', sa.String(128)))
