"""empty message

Revision ID: 2de554fb5eb4
Revises: 28ce1e718f05
Create Date: 2015-12-16 11:08:51.056472

"""

# revision identifiers, used by Alembic.
revision = '2de554fb5eb4'
down_revision = '28ce1e718f05'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.add_column('ps_globals', sa.Column('keep_alive_interval', sa.Integer))


def downgrade():
    op.drop_column('ps_globals', 'keep_alive_interval')
