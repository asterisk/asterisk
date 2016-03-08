"""Add unidentified request options to global

Revision ID: 65eb22eb195
Revises: 1c688d9a003c
Create Date: 2016-03-11 11:58:51.567959

"""

# revision identifiers, used by Alembic.
revision = '65eb22eb195'
down_revision = '1c688d9a003c'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.add_column('ps_globals', sa.Column('unidentified_request_count', sa.Integer))
    op.add_column('ps_globals', sa.Column('unidentified_request_period', sa.Integer))
    op.add_column('ps_globals', sa.Column('unidentified_request_prune_interval', sa.Integer))
    op.add_column('ps_globals', sa.Column('default_realm', sa.String(40)))

def downgrade():
    with op.batch_alter_table('ps_globals') as batch_op:
        batch_op.drop_column('unidentified_request_count')
        batch_op.drop_column('unidentified_request_period')
        batch_op.drop_column('unidentified_request_prune_interval')
        batch_op.drop_column('default_realm')
