"""add_keep_alive_interval

Revision ID: 189a235b3fd7
Revises: 339a3bdf53fc
Create Date: 2015-12-16 11:23:16.994523

"""

# revision identifiers, used by Alembic.
revision = '189a235b3fd7'
down_revision = '339a3bdf53fc'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.add_column('ps_globals', sa.Column('keep_alive_interval', sa.Integer))


def downgrade():
    op.drop_column('ps_globals', 'keep_alive_interval')
