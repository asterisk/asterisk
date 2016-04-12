"""add default_from_user

Revision ID: 154177371065
Revises: 26f10cadc157
Create Date: 2015-09-04 14:13:59.195013

"""

# revision identifiers, used by Alembic.
revision = '154177371065'
down_revision = '26f10cadc157'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.add_column('ps_globals', sa.Column('default_from_user', sa.String(80)))


def downgrade():
    op.drop_column('ps_globals', 'default_from_user')
