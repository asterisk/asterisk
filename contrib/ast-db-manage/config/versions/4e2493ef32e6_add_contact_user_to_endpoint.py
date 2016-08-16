"""Add contact_user to endpoint

Revision ID: 4e2493ef32e6
Revises: 4a6c67fa9b7a
Create Date: 2016-08-16 14:19:58.918466

"""

# revision identifiers, used by Alembic.
revision = '4e2493ef32e6'
down_revision = '4a6c67fa9b7a'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.add_column('ps_endpoints', sa.Column('contact_user', sa.String(80)))


def downgrade():
    op.drop_column('ps_endpoints', 'contact_user')
