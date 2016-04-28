"""Add unidentified request options to global

Revision ID: 65eb22eb195
Revises: 8d478ab86e29
Create Date: 2016-03-11 11:58:51.567959

"""

# revision identifiers, used by Alembic.
revision = '65eb22eb195'
down_revision = '8d478ab86e29'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.add_column('ps_globals', sa.Column('unidentified_request_count', sa.Integer))
    op.add_column('ps_globals', sa.Column('unidentified_request_period', sa.Integer))
    op.add_column('ps_globals', sa.Column('unidentified_request_prune_interval', sa.Integer))
    op.add_column('ps_globals', sa.Column('default_realm', sa.String(40)))

def downgrade():
    op.drop_column('ps_globals', 'unidentified_request_count')
    op.drop_column('ps_globals', 'unidentified_request_period')
    op.drop_column('ps_globals', 'unidentified_request_prune_interval')
    op.drop_column('ps_globals', 'default_realm')
