"""add wrapuptime to queue_members

Revision ID: e2f04d309071
Revises: 041c0d3d1857
Create Date: 2017-12-07 08:32:45.360857

"""

# revision identifiers, used by Alembic.
revision = 'e2f04d309071'
down_revision = '041c0d3d1857'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.add_column('queue_members', sa.Column('wrapuptime', sa.Integer))


def downgrade():
    op.drop_column('queue_members', 'wrapuptime')
