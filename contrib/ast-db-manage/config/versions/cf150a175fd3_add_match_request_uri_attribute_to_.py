"""add match_request_uri attribute to identify

Revision ID: cf150a175fd3
Revises: 8fce8496f03e
Create Date: 2024-03-28 14:19:15.033869

"""

# revision identifiers, used by Alembic.
revision = 'cf150a175fd3'
down_revision = '8fce8496f03e'

from alembic import op
import sqlalchemy as sa

def upgrade():
    op.add_column('ps_endpoint_id_ips', sa.Column('match_request_uri', sa.String(255)))

def downgrade():
    op.drop_column('ps_endpoint_id_ips', 'match_request_uri')
