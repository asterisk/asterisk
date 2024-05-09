"""add transport attribute to identify

Revision ID: d5122576cca8
Revises: cf150a175fd3
Create Date: 2024-03-28 14:29:43.372496

"""

# revision identifiers, used by Alembic.
revision = 'd5122576cca8'
down_revision = 'cf150a175fd3'

from alembic import op
import sqlalchemy as sa

def upgrade():
    op.add_column('ps_endpoint_id_ips', sa.Column('transport', sa.String(128)))

def downgrade():
    op.drop_column('ps_endpoint_id_ips', 'transport')
