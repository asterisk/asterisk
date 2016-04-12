"""add pjsip timeout options

Revision ID: 26f10cadc157
Revises: 498357a710ae
Create Date: 2015-07-21 07:45:00.696965

"""

# revision identifiers, used by Alembic.
revision = '26f10cadc157'
down_revision = '498357a710ae'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.add_column('ps_endpoints', sa.Column('rtp_timeout', sa.Integer))
    op.add_column('ps_endpoints', sa.Column('rtp_timeout_hold', sa.Integer))


def downgrade():
    op.drop_column('ps_endpoints', 'rtp_timeout')
    op.drop_column('ps_endpoints', 'rtp_timeout_hold')
