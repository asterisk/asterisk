"""add fax_detect_timeout option

Revision ID: 4a6c67fa9b7a
Revises: 9deac0ae4717
Create Date: 2016-07-18 18:20:44.249491

"""

# revision identifiers, used by Alembic.
revision = '4a6c67fa9b7a'
down_revision = '9deac0ae4717'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.add_column('ps_endpoints', sa.Column('fax_detect_timeout', sa.Integer))


def downgrade():
    op.drop_column('ps_endpoints', 'fax_detect_timeout')
