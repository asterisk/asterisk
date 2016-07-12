"""pjsip_add_subscribe_context

Revision ID: 9deac0ae4717
Revises: ef7efc2d3964
Create Date: 2016-07-04 12:11:28.117788

"""

# revision identifiers, used by Alembic.
revision = '9deac0ae4717'
down_revision = 'ef7efc2d3964'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.add_column('ps_endpoints', sa.Column('subscribe_context', sa.String(40)))

def downgrade():
    op.drop_column('ps_endpoints', 'subscribe_context')
