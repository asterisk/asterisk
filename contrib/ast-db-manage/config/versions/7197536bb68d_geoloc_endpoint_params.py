"""Geoloc Endpoint Params

Revision ID: 7197536bb68d
Revises: 58e440314c2a
Create Date: 2022-03-07 05:32:54.909429

"""

# revision identifiers, used by Alembic.
revision = '7197536bb68d'
down_revision = '58e440314c2a'

from alembic import op
import sqlalchemy as sa

def upgrade():
    op.add_column('ps_endpoints', sa.Column('geoloc_incoming_call_profile', sa.String(80)))
    op.add_column('ps_endpoints', sa.Column('geoloc_outgoing_call_profile', sa.String(80)))

def downgrade():
    op.drop_column('ps_endpoints', 'geoloc_outgoing_call_profile')
    op.drop_column('ps_endpoints', 'geoloc_incoming_call_profile')
