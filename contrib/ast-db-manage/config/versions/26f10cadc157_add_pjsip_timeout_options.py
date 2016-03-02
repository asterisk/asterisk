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
    with op.batch_alter_table('ps_endpoints') as batch_op:
        batch_op.drop_column('rtp_timeout')
        batch_op.drop_column('rtp_timeout_hold')
