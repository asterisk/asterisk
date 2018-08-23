"""Increase uri column size

Revision ID: 1d3ed26d9978
Revises: 19b00bc19b7b
Create Date: 2018-08-23 11:46:13.283801

"""

# revision identifiers, used by Alembic.
revision = '1d3ed26d9978'
down_revision = '19b00bc19b7b'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.alter_column('ps_contacts', 'uri', type_=sa.String(511))


def downgrade():
    op.alter_column('ps_contacts', 'uri', type_=sa.String(255))
