"""add_keep_alive_interval

Revision ID: 189a235b3fd7
Revises: 28ce1e718f05
Create Date: 2015-12-16 11:23:16.994523

"""

# revision identifiers, used by Alembic.
revision = '189a235b3fd7'
down_revision = '28ce1e718f05'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.add_column('ps_globals', sa.Column('keep_alive_interval', sa.Integer))


def downgrade():
    with op.batch_alter_table('ps_globals') as batch_op:
        batch_op.drop_column('keep_alive_interval')
